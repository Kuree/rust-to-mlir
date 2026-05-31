//===- RustTypedToArith.cpp - Rust typed to arith conversion ---*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Conversion/RustTypedToArith/RustTypedToArith.h"

#include "RustToLLVM/Support/OpCreateCompat.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustTypes.h"
#include "mlir/Dialect/RustTyped/IR/RustTypedOps.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/APInt.h"

namespace mlir {
#define GEN_PASS_DEF_CONVERTRUSTTYPEDTOARITHPASS
#include "mlir/Conversion/RustTypedToArith/RustTypedToArithPasses.h.inc"
} // namespace mlir

using namespace mlir;
namespace rustmir = mlir::rust::mir;

namespace {
unsigned getPointerWidth(ModuleOp module) {
  if (auto attr =
          module->getAttrOfType<IntegerAttr>("rust.mir.target_pointer_width")) {
    int64_t width = attr.getInt();
    if (width > 0)
      return static_cast<unsigned>(width);
  }

  DataLayout layout(module);
  llvm::TypeSize indexBits =
      layout.getTypeSizeInBits(IndexType::get(module.getContext()));
  if (!indexBits.isScalable() && indexBits.getFixedValue() > 0)
    return static_cast<unsigned>(indexBits.getFixedValue());

  return 64;
}

bool isSignedRustInteger(Type type) {
  if (auto intType = dyn_cast<rustmir::IntType>(type))
    return intType.getIsSigned();
  // Bool (i1) and other non-integers use unsigned semantics.
  return false;
}

class RustScalarTypeConverter : public TypeConverter {
public:
  explicit RustScalarTypeConverter(MLIRContext *context, unsigned pointerWidth)
      : context(context), pointerWidth(pointerWidth) {
    addConversion([](Type type) { return type; });
    addConversion([this](rustmir::BoolType) -> Type {
      return IntegerType::get(this->context, 1);
    });
    addConversion([this](rustmir::IntType type) -> Type {
      return IntegerType::get(this->context,
                              type.getBitWidth(this->pointerWidth));
    });
    addConversion([this](rustmir::SlotType type) -> Type {
      Type elementType = convertType(type.getElementType());
      if (!elementType)
        return Type();
      return rustmir::SlotType::get(this->context, elementType);
    });
    addConversion([this](rustmir::TypedAddrType type) -> Type {
      Type elementType = convertType(type.getElementType());
      if (!elementType)
        return Type();
      return rustmir::TypedAddrType::get(this->context, elementType);
    });
    addConversion([this](rustmir::TypedTupleType type) -> Type {
      SmallVector<Type> elementTypes;
      elementTypes.reserve(type.getElementTypes().size());
      for (Type elementType : type.getElementTypes()) {
        Type converted = convertType(elementType);
        if (!converted)
          return Type();
        elementTypes.push_back(converted);
      }
      return LLVM::LLVMStructType::getLiteral(this->context, elementTypes);
    });
    addConversion([this](rustmir::TypedArrayType type) -> Type {
      Type elementType = convertType(type.getElementType());
      if (!elementType)
        return Type();
      return LLVM::LLVMArrayType::get(this->context, elementType,
                                      type.getLength());
    });
    addConversion([this](rustmir::AdtType type) -> Type {
      // A single-variant ADT lowers to its variant tuple's LLVM struct; enums
      // (multiple variants) are not yet lowered as aggregates.
      ArrayRef<Type> variants = type.getVariants();
      if (variants.size() != 1)
        return Type();
      return convertType(variants.front());
    });
    addConversion([this](rustmir::TypedSliceType type) -> Type {
      Type elementType = convertType(type.getElementType());
      if (!elementType)
        return Type();
      return rustmir::TypedSliceType::get(this->context, elementType);
    });
    addConversion([this](rustmir::TypedRefType type) -> Type {
      Type pointeeType = convertType(type.getPointeeType());
      if (!pointeeType)
        return Type();
      return rustmir::TypedRefType::get(this->context, type.getMutability(),
                                        pointeeType);
    });
    addConversion([this](rustmir::TypedRawPtrType type) -> Type {
      Type pointeeType = convertType(type.getPointeeType());
      if (!pointeeType)
        return Type();
      return rustmir::TypedRawPtrType::get(this->context, type.getMutability(),
                                           pointeeType);
    });
  }

private:
  MLIRContext *context;
  unsigned pointerWidth;
};

bool needsTypeConversion(Type type, const TypeConverter &converter) {
  Type converted = converter.convertType(type);
  return converted && converted != type;
}

bool isIntegerLikeAfterConversion(Type type, const TypeConverter &converter) {
  Type converted = converter.convertType(type);
  return converted && isa<IntegerType>(converted);
}

bool isLowerableConst(rustmir::TypedConstOp op,
                      const TypeConverter &converter) {
  return isIntegerLikeAfterConversion(op.getResult().getType(), converter);
}

template <typename OpT>
bool isLowerableBinaryOp(OpT op, const TypeConverter &converter) {
  return isIntegerLikeAfterConversion(op.getLhs().getType(), converter) &&
         isIntegerLikeAfterConversion(op.getRhs().getType(), converter) &&
         isIntegerLikeAfterConversion(op.getResult().getType(), converter);
}

template <typename OpT>
bool isLowerableCheckedBinaryOp(OpT op, const TypeConverter &converter) {
  Type lhsType = converter.convertType(op.getLhs().getType());
  Type rhsType = converter.convertType(op.getRhs().getType());
  Type valueType = converter.convertType(op.getValue().getType());
  Type overflowType = converter.convertType(op.getOverflow().getType());
  return isa_and_nonnull<IntegerType>(lhsType) && lhsType == rhsType &&
         lhsType == valueType && overflowType &&
         overflowType.isSignlessInteger(1);
}

template <typename OpT>
bool isLowerableUnaryOp(OpT op, const TypeConverter &converter) {
  return isIntegerLikeAfterConversion(op.getInput().getType(), converter) &&
         isIntegerLikeAfterConversion(op.getResult().getType(), converter);
}

bool isLowerableMakeAggregate(rustmir::MakeAggregateOp op,
                              const TypeConverter &converter) {
  Type converted = converter.convertType(op.getResult().getType());
  if (auto structType = dyn_cast_or_null<LLVM::LLVMStructType>(converted))
    return structType.getBody().size() == op.getValues().size();
  if (auto arrayType = dyn_cast_or_null<LLVM::LLVMArrayType>(converted))
    return arrayType.getNumElements() == op.getValues().size();
  return false;
}

bool isLowerableField(rustmir::FieldOp op, const TypeConverter &converter) {
  Type aggregateType = converter.convertType(op.getAggregate().getType());
  int64_t index = static_cast<int64_t>(op.getIndex());
  if (auto structType = dyn_cast_or_null<LLVM::LLVMStructType>(aggregateType))
    return index >= 0 &&
           static_cast<size_t>(index) < structType.getBody().size();
  if (auto arrayType = dyn_cast_or_null<LLVM::LLVMArrayType>(aggregateType))
    return index >= 0 &&
           static_cast<uint64_t>(index) < arrayType.getNumElements();
  return false;
}

std::optional<llvm::APInt> parseIntegerLiteral(StringRef text, unsigned width) {
  text = text.trim();
  if (text.consume_front("const"))
    text = text.trim();

  size_t space = text.find_first_of(" \t\r\n");
  if (space != StringRef::npos)
    text = text.take_front(space);

  size_t suffix = text.find('_');
  if (suffix != StringRef::npos)
    text = text.take_front(suffix);

  bool negative = text.consume_front("-");
  if (text.empty())
    return std::nullopt;

  llvm::APInt value;
  if (text.getAsInteger(10, value))
    return std::nullopt;

  value = value.zextOrTrunc(width);
  if (negative)
    value = -value;
  return value;
}

TypedAttr buildIntegerAttr(rustmir::TypedConstOp op, IntegerType resultType) {
  unsigned width = resultType.getWidth();
  if (auto integerAttr = dyn_cast_or_null<IntegerAttr>(op.getValueAttr())) {
    llvm::APInt value = integerAttr.getValue().zextOrTrunc(width);
    return cast<TypedAttr>(IntegerAttr::get(resultType, value));
  }

  std::optional<StringRef> debug = op.getDebug();
  if (!debug)
    return {};

  StringRef text = debug->trim();
  if (text == "true")
    return cast<TypedAttr>(IntegerAttr::get(resultType, 1));
  if (text == "false")
    return cast<TypedAttr>(IntegerAttr::get(resultType, 0));

  std::optional<llvm::APInt> value = parseIntegerLiteral(text, width);
  if (!value)
    return {};
  return cast<TypedAttr>(IntegerAttr::get(resultType, *value));
}

Value createIntegerConstant(OpBuilder &builder, Location loc, IntegerType type,
                            const llvm::APInt &value) {
  auto attr = cast<TypedAttr>(IntegerAttr::get(type, value));
  return mlir::rust::createOp<arith::ConstantOp>(builder, loc, type, attr)
      .getResult();
}

Value createBoolConstant(OpBuilder &builder, Location loc, bool value) {
  return mlir::rust::createOp<arith::ConstantOp>(builder, loc,
                                                 builder.getBoolAttr(value))
      .getResult();
}

void createNoOverflowAssert(OpBuilder &builder, Location loc, Value overflow,
                            StringRef message) {
  Value trueValue = createBoolConstant(builder, loc, true);
  Value noOverflow =
      mlir::rust::createOp<arith::XOrIOp>(builder, loc, overflow, trueValue)
          .getResult();
  mlir::rust::createOp<cf::AssertOp>(builder, loc, noOverflow, message);
}

struct TypedConstOpConversion
    : public OpConversionPattern<rustmir::TypedConstOp> {
  using OpConversionPattern<rustmir::TypedConstOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::TypedConstOp op, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type convertedType =
        getTypeConverter()->convertType(op.getResult().getType());
    auto resultType = dyn_cast_or_null<IntegerType>(convertedType);
    if (!resultType)
      return failure();

    if (op.getDebug() && op.getDebug()->trim() == "uninit") {
      Value poison = mlir::rust::createOp<ub::PoisonOp>(
                         rewriter, op.getLoc(), resultType,
                         ub::PoisonAttr::get(rewriter.getContext()))
                         .getResult();
      rewriter.replaceOp(op, poison);
      return success();
    }

    TypedAttr value = buildIntegerAttr(op, resultType);
    if (!value)
      return op.emitError("expected an integer or boolean rust.typed.const");

    Value constant = mlir::rust::createOp<arith::ConstantOp>(
                         rewriter, op.getLoc(), resultType, value)
                         .getResult();
    rewriter.replaceOp(op, constant);
    return success();
  }
};

template <typename SourceOp, typename TargetOp>
struct SimpleBinaryOpConversion : public OpConversionPattern<SourceOp> {
  using OpConversionPattern<SourceOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(SourceOp op, typename SourceOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    if (!this->getTypeConverter()->convertType(op.getResult().getType()))
      return failure();
    Value replacement =
        mlir::rust::createOp<TargetOp>(rewriter, op.getLoc(), adaptor.getLhs(),
                                       adaptor.getRhs())
            .getResult();
    rewriter.replaceOp(op, replacement);
    return success();
  }
};

template <typename SourceOp>
struct DivOpConversion : public OpConversionPattern<SourceOp> {
  using OpConversionPattern<SourceOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(SourceOp op, typename SourceOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    if (!this->getTypeConverter()->convertType(op.getResult().getType()))
      return failure();
    Value replacement =
        isSignedRustInteger(op.getLhs().getType())
            ? mlir::rust::createOp<arith::DivSIOp>(
                  rewriter, op.getLoc(), adaptor.getLhs(), adaptor.getRhs())
                  .getResult()
            : mlir::rust::createOp<arith::DivUIOp>(
                  rewriter, op.getLoc(), adaptor.getLhs(), adaptor.getRhs())
                  .getResult();
    rewriter.replaceOp(op, replacement);
    return success();
  }
};

template <typename SourceOp>
struct RemOpConversion : public OpConversionPattern<SourceOp> {
  using OpConversionPattern<SourceOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(SourceOp op, typename SourceOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    if (!this->getTypeConverter()->convertType(op.getResult().getType()))
      return failure();
    Value replacement =
        isSignedRustInteger(op.getLhs().getType())
            ? mlir::rust::createOp<arith::RemSIOp>(
                  rewriter, op.getLoc(), adaptor.getLhs(), adaptor.getRhs())
                  .getResult()
            : mlir::rust::createOp<arith::RemUIOp>(
                  rewriter, op.getLoc(), adaptor.getLhs(), adaptor.getRhs())
                  .getResult();
    rewriter.replaceOp(op, replacement);
    return success();
  }
};

template <typename SourceOp>
struct ShrOpConversion : public OpConversionPattern<SourceOp> {
  using OpConversionPattern<SourceOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(SourceOp op, typename SourceOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    if (!this->getTypeConverter()->convertType(op.getResult().getType()))
      return failure();
    Value replacement =
        isSignedRustInteger(op.getLhs().getType())
            ? mlir::rust::createOp<arith::ShRSIOp>(
                  rewriter, op.getLoc(), adaptor.getLhs(), adaptor.getRhs())
                  .getResult()
            : mlir::rust::createOp<arith::ShRUIOp>(
                  rewriter, op.getLoc(), adaptor.getLhs(), adaptor.getRhs())
                  .getResult();
    rewriter.replaceOp(op, replacement);
    return success();
  }
};

template <typename SourceOp, arith::CmpIPredicate signedPredicate,
          arith::CmpIPredicate unsignedPredicate>
struct CompareOpConversion : public OpConversionPattern<SourceOp> {
  using OpConversionPattern<SourceOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(SourceOp op, typename SourceOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    if (!this->getTypeConverter()->convertType(op.getResult().getType()))
      return failure();
    arith::CmpIPredicate predicate = isSignedRustInteger(op.getLhs().getType())
                                         ? signedPredicate
                                         : unsignedPredicate;
    Value replacement =
        mlir::rust::createOp<arith::CmpIOp>(rewriter, op.getLoc(), predicate,
                                            adaptor.getLhs(), adaptor.getRhs())
            .getResult();
    rewriter.replaceOp(op, replacement);
    return success();
  }
};

template <typename OpT>
LogicalResult replaceCheckedAdd(OpT op, Value lhs, Value rhs, Type rustLhsType,
                                IntegerType resultType,
                                ConversionPatternRewriter &rewriter) {
  Location loc = op.getLoc();
  if (!isSignedRustInteger(rustLhsType)) {
    auto extended =
        mlir::rust::createOp<arith::AddUIExtendedOp>(rewriter, loc, lhs, rhs);
    createNoOverflowAssert(rewriter, loc, extended.getOverflow(),
                           "attempt to add with overflow");
    SmallVector<Value, 2> replacements{extended.getSum(),
                                       extended.getOverflow()};
    rewriter.replaceOp(op, replacements);
    return success();
  }

  Value zero = createIntegerConstant(
      rewriter, loc, resultType, llvm::APInt::getZero(resultType.getWidth()));
  Value value =
      mlir::rust::createOp<arith::AddIOp>(rewriter, loc, lhs, rhs).getResult();
  Value lhsXorValue =
      mlir::rust::createOp<arith::XOrIOp>(rewriter, loc, lhs, value)
          .getResult();
  Value rhsXorValue =
      mlir::rust::createOp<arith::XOrIOp>(rewriter, loc, rhs, value)
          .getResult();
  Value overflowBits = mlir::rust::createOp<arith::AndIOp>(
                           rewriter, loc, lhsXorValue, rhsXorValue)
                           .getResult();
  Value overflow =
      mlir::rust::createOp<arith::CmpIOp>(
          rewriter, loc, arith::CmpIPredicate::slt, overflowBits, zero)
          .getResult();
  createNoOverflowAssert(rewriter, loc, overflow,
                         "attempt to add with overflow");
  SmallVector<Value, 2> replacements{value, overflow};
  rewriter.replaceOp(op, replacements);
  return success();
}

template <typename OpT>
LogicalResult replaceCheckedSub(OpT op, Value lhs, Value rhs, Type rustLhsType,
                                IntegerType resultType,
                                ConversionPatternRewriter &rewriter) {
  Location loc = op.getLoc();
  Value value =
      mlir::rust::createOp<arith::SubIOp>(rewriter, loc, lhs, rhs).getResult();
  Value overflow;
  if (isSignedRustInteger(rustLhsType)) {
    Value zero = createIntegerConstant(
        rewriter, loc, resultType, llvm::APInt::getZero(resultType.getWidth()));
    Value lhsXorRhs =
        mlir::rust::createOp<arith::XOrIOp>(rewriter, loc, lhs, rhs)
            .getResult();
    Value lhsXorValue =
        mlir::rust::createOp<arith::XOrIOp>(rewriter, loc, lhs, value)
            .getResult();
    Value overflowBits = mlir::rust::createOp<arith::AndIOp>(
                             rewriter, loc, lhsXorRhs, lhsXorValue)
                             .getResult();
    overflow = mlir::rust::createOp<arith::CmpIOp>(
                   rewriter, loc, arith::CmpIPredicate::slt, overflowBits, zero)
                   .getResult();
  } else {
    overflow = mlir::rust::createOp<arith::CmpIOp>(
                   rewriter, loc, arith::CmpIPredicate::ult, lhs, rhs)
                   .getResult();
  }

  createNoOverflowAssert(rewriter, loc, overflow,
                         "attempt to subtract with overflow");
  SmallVector<Value, 2> replacements{value, overflow};
  rewriter.replaceOp(op, replacements);
  return success();
}

template <typename OpT>
LogicalResult replaceCheckedMul(OpT op, Value lhs, Value rhs, Type rustLhsType,
                                IntegerType resultType,
                                ConversionPatternRewriter &rewriter) {
  Location loc = op.getLoc();
  if (isSignedRustInteger(rustLhsType)) {
    auto extended =
        mlir::rust::createOp<arith::MulSIExtendedOp>(rewriter, loc, lhs, rhs);
    Value shiftAmount = createIntegerConstant(
        rewriter, loc, resultType,
        llvm::APInt(resultType.getWidth(), resultType.getWidth() - 1));
    Value signFill = mlir::rust::createOp<arith::ShRSIOp>(
                         rewriter, loc, extended.getLow(), shiftAmount)
                         .getResult();
    Value overflow = mlir::rust::createOp<arith::CmpIOp>(
                         rewriter, loc, arith::CmpIPredicate::ne,
                         extended.getHigh(), signFill)
                         .getResult();
    createNoOverflowAssert(rewriter, loc, overflow,
                           "attempt to multiply with overflow");
    SmallVector<Value, 2> replacements{extended.getLow(), overflow};
    rewriter.replaceOp(op, replacements);
    return success();
  }

  auto extended =
      mlir::rust::createOp<arith::MulUIExtendedOp>(rewriter, loc, lhs, rhs);
  Value zero = createIntegerConstant(
      rewriter, loc, resultType, llvm::APInt::getZero(resultType.getWidth()));
  Value overflow = mlir::rust::createOp<arith::CmpIOp>(rewriter, loc,
                                                       arith::CmpIPredicate::ne,
                                                       extended.getHigh(), zero)
                       .getResult();
  createNoOverflowAssert(rewriter, loc, overflow,
                         "attempt to multiply with overflow");
  SmallVector<Value, 2> replacements{extended.getLow(), overflow};
  rewriter.replaceOp(op, replacements);
  return success();
}

struct CheckedAddOpConversion
    : public OpConversionPattern<rustmir::CheckedAddOp> {
  using OpConversionPattern<rustmir::CheckedAddOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::CheckedAddOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    auto resultType = dyn_cast_or_null<IntegerType>(
        getTypeConverter()->convertType(op.getValue().getType()));
    if (!resultType)
      return failure();
    return replaceCheckedAdd(op, adaptor.getLhs(), adaptor.getRhs(),
                             op.getLhs().getType(), resultType, rewriter);
  }
};

struct CheckedSubOpConversion
    : public OpConversionPattern<rustmir::CheckedSubOp> {
  using OpConversionPattern<rustmir::CheckedSubOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::CheckedSubOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    auto resultType = dyn_cast_or_null<IntegerType>(
        getTypeConverter()->convertType(op.getValue().getType()));
    if (!resultType)
      return failure();
    return replaceCheckedSub(op, adaptor.getLhs(), adaptor.getRhs(),
                             op.getLhs().getType(), resultType, rewriter);
  }
};

struct CheckedMulOpConversion
    : public OpConversionPattern<rustmir::CheckedMulOp> {
  using OpConversionPattern<rustmir::CheckedMulOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::CheckedMulOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    auto resultType = dyn_cast_or_null<IntegerType>(
        getTypeConverter()->convertType(op.getValue().getType()));
    if (!resultType)
      return failure();
    return replaceCheckedMul(op, adaptor.getLhs(), adaptor.getRhs(),
                             op.getLhs().getType(), resultType, rewriter);
  }
};

struct NegOpConversion : public OpConversionPattern<rustmir::NegOp> {
  using OpConversionPattern<rustmir::NegOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::NegOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    auto resultType = dyn_cast_or_null<IntegerType>(
        getTypeConverter()->convertType(op.getResult().getType()));
    if (!resultType)
      return failure();

    Value zero =
        createIntegerConstant(rewriter, op.getLoc(), resultType,
                              llvm::APInt::getZero(resultType.getWidth()));
    Value replacement = mlir::rust::createOp<arith::SubIOp>(
                            rewriter, op.getLoc(), zero, adaptor.getInput())
                            .getResult();
    rewriter.replaceOp(op, replacement);
    return success();
  }
};

struct NotOpConversion : public OpConversionPattern<rustmir::NotOp> {
  using OpConversionPattern<rustmir::NotOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::NotOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    auto resultType = dyn_cast_or_null<IntegerType>(
        getTypeConverter()->convertType(op.getResult().getType()));
    if (!resultType)
      return failure();

    Value allOnes =
        createIntegerConstant(rewriter, op.getLoc(), resultType,
                              llvm::APInt::getAllOnes(resultType.getWidth()));
    Value replacement = mlir::rust::createOp<arith::XOrIOp>(
                            rewriter, op.getLoc(), adaptor.getInput(), allOnes)
                            .getResult();
    rewriter.replaceOp(op, replacement);
    return success();
  }
};

struct LocalSlotConversion : public OpConversionPattern<rustmir::LocalSlotOp> {
  using OpConversionPattern<rustmir::LocalSlotOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::LocalSlotOp op, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type convertedType =
        getTypeConverter()->convertType(op.getSlot().getType());
    auto slotType = dyn_cast_or_null<rustmir::SlotType>(convertedType);
    if (!slotType)
      return failure();

    auto newOp = mlir::rust::createOp<rustmir::LocalSlotOp>(
        rewriter, op.getLoc(), slotType, op.getIndexAttr(), op.getNameAttr(),
        op.getMutabilityAttr(), op.getRoleAttr(), op.getSpanAttr(),
        op.getAddressTakenAttr());
    rewriter.replaceOp(op, newOp.getSlot());
    return success();
  }
};

struct LoadConversion : public OpConversionPattern<rustmir::LoadOp> {
  using OpConversionPattern<rustmir::LoadOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::LoadOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getValue().getType());
    if (!resultType)
      return failure();
    auto newOp = mlir::rust::createOp<rustmir::LoadOp>(
        rewriter, op.getLoc(), resultType, adaptor.getSlot());
    rewriter.replaceOp(op, newOp.getValue());
    return success();
  }
};

struct StoreConversion : public OpConversionPattern<rustmir::StoreOp> {
  using OpConversionPattern<rustmir::StoreOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::StoreOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    mlir::rust::createOp<rustmir::StoreOp>(
        rewriter, op.getLoc(), adaptor.getValue(), adaptor.getSlot());
    rewriter.eraseOp(op);
    return success();
  }
};

struct BorrowOpConversion : public OpConversionPattern<rustmir::BorrowOp> {
  using OpConversionPattern<rustmir::BorrowOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::BorrowOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!resultType)
      return failure();
    auto newOp = mlir::rust::createOp<rustmir::BorrowOp>(
        rewriter, op.getLoc(), resultType, adaptor.getSlot(),
        op.getBorrowKindAttr(), op.getMutabilityAttr(), op.getRustRegionAttr(),
        op.getSpanAttr());
    rewriter.replaceOp(op, newOp.getResult());
    return success();
  }
};

struct RawAddressOpConversion
    : public OpConversionPattern<rustmir::RawAddressOp> {
  using OpConversionPattern<rustmir::RawAddressOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::RawAddressOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!resultType)
      return failure();
    auto newOp = mlir::rust::createOp<rustmir::RawAddressOp>(
        rewriter, op.getLoc(), resultType, adaptor.getSlot(),
        op.getRawPtrKindAttr(), op.getMutabilityAttr(), op.getSpanAttr());
    rewriter.replaceOp(op, newOp.getResult());
    return success();
  }
};

struct MakeAggregateConversion
    : public OpConversionPattern<rustmir::MakeAggregateOp> {
  using OpConversionPattern<rustmir::MakeAggregateOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::MakeAggregateOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!resultType || (!isa<LLVM::LLVMStructType>(resultType) &&
                        !isa<LLVM::LLVMArrayType>(resultType)))
      return failure();

    Value aggregate =
        mlir::rust::createOp<LLVM::UndefOp>(rewriter, op.getLoc(), resultType)
            .getRes();
    for (auto [index, value] : llvm::enumerate(adaptor.getValues())) {
      int64_t position = static_cast<int64_t>(index);
      aggregate = mlir::rust::createOp<LLVM::InsertValueOp>(
                      rewriter, op.getLoc(), aggregate, value,
                      ArrayRef<int64_t>(position))
                      .getRes();
    }
    rewriter.replaceOp(op, aggregate);
    return success();
  }
};

struct FieldConversion : public OpConversionPattern<rustmir::FieldOp> {
  using OpConversionPattern<rustmir::FieldOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::FieldOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!resultType)
      return failure();
    int64_t position = static_cast<int64_t>(op.getIndex());
    Value field = mlir::rust::createOp<LLVM::ExtractValueOp>(
                      rewriter, op.getLoc(), resultType, adaptor.getAggregate(),
                      ArrayRef<int64_t>(position))
                      .getRes();
    rewriter.replaceOp(op, field);
    return success();
  }
};

struct FieldAddrConversion : public OpConversionPattern<rustmir::FieldAddrOp> {
  using OpConversionPattern<rustmir::FieldAddrOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::FieldAddrOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType =
        getTypeConverter()->convertType(op.getAddress().getType());
    auto addrType = dyn_cast_or_null<rustmir::TypedAddrType>(resultType);
    if (!addrType)
      return failure();
    auto newOp = mlir::rust::createOp<rustmir::FieldAddrOp>(
        rewriter, op.getLoc(), addrType, adaptor.getBase(), op.getIndexAttr(),
        op.getSpanAttr());
    rewriter.replaceOp(op, newOp.getAddress());
    return success();
  }
};

struct IndexAddrConversion : public OpConversionPattern<rustmir::IndexAddrOp> {
  using OpConversionPattern<rustmir::IndexAddrOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::IndexAddrOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType =
        getTypeConverter()->convertType(op.getAddress().getType());
    auto addrType = dyn_cast_or_null<rustmir::TypedAddrType>(resultType);
    if (!addrType)
      return failure();
    auto newOp = mlir::rust::createOp<rustmir::IndexAddrOp>(
        rewriter, op.getLoc(), addrType, adaptor.getBase(),
        adaptor.getIndex(), op.getSpanAttr());
    rewriter.replaceOp(op, newOp.getAddress());
    return success();
  }
};

struct SliceFromArrayConversion
    : public OpConversionPattern<rustmir::SliceFromArrayOp> {
  using OpConversionPattern<rustmir::SliceFromArrayOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::SliceFromArrayOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!resultType)
      return failure();
    auto newOp = mlir::rust::createOp<rustmir::SliceFromArrayOp>(
        rewriter, op.getLoc(), resultType, adaptor.getSource(),
        op.getSpanAttr());
    rewriter.replaceOp(op, newOp.getResult());
    return success();
  }
};

struct PtrMetadataConversion
    : public OpConversionPattern<rustmir::PtrMetadataOp> {
  using OpConversionPattern<rustmir::PtrMetadataOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::PtrMetadataOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType =
        getTypeConverter()->convertType(op.getMetadata().getType());
    if (!resultType)
      return failure();
    auto newOp = mlir::rust::createOp<rustmir::PtrMetadataOp>(
        rewriter, op.getLoc(), resultType, adaptor.getSource(),
        op.getSpanAttr());
    rewriter.replaceOp(op, newOp.getMetadata());
    return success();
  }
};

struct SubsliceConversion : public OpConversionPattern<rustmir::SubsliceOp> {
  using OpConversionPattern<rustmir::SubsliceOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::SubsliceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!resultType)
      return failure();
    auto newOp = mlir::rust::createOp<rustmir::SubsliceOp>(
        rewriter, op.getLoc(), resultType, adaptor.getSource(),
        op.getFromIndexAttr(), op.getToIndexAttr(), op.getFromEndAttr(),
        op.getSpanAttr());
    rewriter.replaceOp(op, newOp.getResult());
    return success();
  }
};

struct SliceRangeConversion
    : public OpConversionPattern<rustmir::SliceRangeOp> {
  using OpConversionPattern<rustmir::SliceRangeOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::SliceRangeOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!resultType)
      return failure();
    auto newOp = mlir::rust::createOp<rustmir::SliceRangeOp>(
        rewriter, op.getLoc(), resultType, adaptor.getSource(),
        adaptor.getStart(), adaptor.getLength(), op.getSpanAttr());
    rewriter.replaceOp(op, newOp.getResult());
    return success();
  }
};

struct TypedReturnConversion
    : public OpConversionPattern<rustmir::TypedReturnOp> {
  using OpConversionPattern<rustmir::TypedReturnOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::TypedReturnOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    mlir::rust::createOp<rustmir::TypedReturnOp>(
        rewriter, op.getLoc(), adaptor.getValues(), op.getSpanAttr());
    rewriter.eraseOp(op);
    return success();
  }
};

struct TypedSwitchIntConversion
    : public OpConversionPattern<rustmir::TypedSwitchIntOp> {
  using OpConversionPattern<rustmir::TypedSwitchIntOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::TypedSwitchIntOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    mlir::rust::createOp<rustmir::TypedSwitchIntOp>(
        rewriter, op.getLoc(), adaptor.getDiscr(), op.getTargetsAttr(),
        op.getSpanAttr());
    rewriter.eraseOp(op);
    return success();
  }
};

struct TypedAssertConversion
    : public OpConversionPattern<rustmir::TypedAssertOp> {
  using OpConversionPattern<rustmir::TypedAssertOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::TypedAssertOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    mlir::rust::createOp<rustmir::TypedAssertOp>(
        rewriter, op.getLoc(), adaptor.getCond(), op.getMsgAttr(),
        op.getSpanAttr());
    rewriter.eraseOp(op);
    return success();
  }
};

struct TypedCallConversion : public OpConversionPattern<rustmir::TypedCallOp> {
  using OpConversionPattern<rustmir::TypedCallOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::TypedCallOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    SmallVector<Type> resultTypes;
    if (failed(
            getTypeConverter()->convertTypes(op.getResultTypes(), resultTypes)))
      return failure();

    auto converted = mlir::rust::createOp<rustmir::TypedCallOp>(
        rewriter, op.getLoc(), resultTypes, op.getCalleeAttr(),
        adaptor.getArgs(), op.getRustNameAttr(), op.getAbiAttr(),
        op.getCalleeDefAttr(), op.getCalleeTypeAttr(),
        op.getCalleeGenericArgsAttr(), op.getCalleeInputsAttr(),
        op.getCalleeOutputAttr(), op.getCVariadicAttr(), op.getTargetAttr(),
        op.getUnwindAttr(), op.getSpanAttr());
    for (NamedAttribute attr : op->getAttrs())
      if (!converted->hasAttr(attr.getName()))
        converted->setAttr(attr.getName(), attr.getValue());
    rewriter.replaceOp(op, converted->getResults());
    return success();
  }
};

template <typename OpT>
void addBinaryOpLegality(ConversionTarget &target,
                         const TypeConverter *typeConverter) {
  target.addDynamicallyLegalOp<OpT>([typeConverter](OpT op) {
    return !isLowerableBinaryOp(op, *typeConverter);
  });
}

template <typename OpT>
void addUnaryOpLegality(ConversionTarget &target,
                        const TypeConverter *typeConverter) {
  target.addDynamicallyLegalOp<OpT>([typeConverter](OpT op) {
    return !isLowerableUnaryOp(op, *typeConverter);
  });
}

struct ConvertRustTypedToArithPass
    : public mlir::impl::ConvertRustTypedToArithPassBase<
          ConvertRustTypedToArithPass> {
  void getDependentDialects(DialectRegistry &registry) const final {
    registry
        .insert<arith::ArithDialect, LLVM::LLVMDialect, cf::ControlFlowDialect,
                rustmir::RustMIRDialect, ub::UBDialect>();
  }

  void runOnOperation() final {
    ModuleOp module = getOperation();
    MLIRContext *context = module.getContext();
    RustScalarTypeConverter typeConverter(context, getPointerWidth(module));

    ConversionTarget target(*context);
    target.addLegalDialect<arith::ArithDialect, LLVM::LLVMDialect,
                           cf::ControlFlowDialect, rustmir::RustMIRDialect,
                           ub::UBDialect>();
    const TypeConverter *typeConverterPtr = &typeConverter;
    target.addDynamicallyLegalOp<rustmir::TypedConstOp>(
        [&](rustmir::TypedConstOp op) {
          return !isLowerableConst(op, typeConverter);
        });
    addBinaryOpLegality<rustmir::AddOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::AddUncheckedOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::SubOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::SubUncheckedOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::MulOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::MulUncheckedOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::DivOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::RemOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::BitAndOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::BitOrOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::BitXorOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::ShlOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::ShlUncheckedOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::ShrOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::ShrUncheckedOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::EqOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::NeOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::LtOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::LeOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::GtOp>(target, typeConverterPtr);
    addBinaryOpLegality<rustmir::GeOp>(target, typeConverterPtr);
    target.addDynamicallyLegalOp<rustmir::CheckedAddOp>(
        [&](rustmir::CheckedAddOp op) {
          return !isLowerableCheckedBinaryOp(op, typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::CheckedSubOp>(
        [&](rustmir::CheckedSubOp op) {
          return !isLowerableCheckedBinaryOp(op, typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::CheckedMulOp>(
        [&](rustmir::CheckedMulOp op) {
          return !isLowerableCheckedBinaryOp(op, typeConverter);
        });
    addUnaryOpLegality<rustmir::NegOp>(target, typeConverterPtr);
    addUnaryOpLegality<rustmir::NotOp>(target, typeConverterPtr);
    target.addDynamicallyLegalOp<rustmir::LocalSlotOp>(
        [&](rustmir::LocalSlotOp op) {
          return !needsTypeConversion(op.getSlot().getType(), typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::LoadOp>([&](rustmir::LoadOp op) {
      return !needsTypeConversion(op.getSlot().getType(), typeConverter) &&
             !needsTypeConversion(op.getValue().getType(), typeConverter);
    });
    target.addDynamicallyLegalOp<rustmir::StoreOp>([&](rustmir::StoreOp op) {
      return !needsTypeConversion(op.getValue().getType(), typeConverter) &&
             !needsTypeConversion(op.getSlot().getType(), typeConverter);
    });
    target.addDynamicallyLegalOp<rustmir::BorrowOp>([&](rustmir::BorrowOp op) {
      return !needsTypeConversion(op.getSlot().getType(), typeConverter) &&
             !needsTypeConversion(op.getResult().getType(), typeConverter);
    });
    target.addDynamicallyLegalOp<rustmir::RawAddressOp>(
        [&](rustmir::RawAddressOp op) {
          return !needsTypeConversion(op.getSlot().getType(), typeConverter) &&
                 !needsTypeConversion(op.getResult().getType(), typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::MakeAggregateOp>(
        [&](rustmir::MakeAggregateOp op) {
          return !isLowerableMakeAggregate(op, typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::FieldOp>([&](rustmir::FieldOp op) {
      return !isLowerableField(op, typeConverter);
    });
    target.addDynamicallyLegalOp<rustmir::FieldAddrOp>(
        [&](rustmir::FieldAddrOp op) {
          return !needsTypeConversion(op.getBase().getType(), typeConverter) &&
                 !needsTypeConversion(op.getAddress().getType(), typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::IndexAddrOp>(
        [&](rustmir::IndexAddrOp op) {
          return !needsTypeConversion(op.getBase().getType(), typeConverter) &&
                 !needsTypeConversion(op.getIndex().getType(), typeConverter) &&
                 !needsTypeConversion(op.getAddress().getType(), typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::SliceFromArrayOp>(
        [&](rustmir::SliceFromArrayOp op) {
          return !needsTypeConversion(op.getSource().getType(),
                                      typeConverter) &&
                 !needsTypeConversion(op.getResult().getType(), typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::PtrMetadataOp>(
        [&](rustmir::PtrMetadataOp op) {
          return !needsTypeConversion(op.getSource().getType(),
                                      typeConverter) &&
                 !needsTypeConversion(op.getMetadata().getType(),
                                      typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::SubsliceOp>(
        [&](rustmir::SubsliceOp op) {
          return !needsTypeConversion(op.getSource().getType(),
                                      typeConverter) &&
                 !needsTypeConversion(op.getResult().getType(), typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::SliceRangeOp>(
        [&](rustmir::SliceRangeOp op) {
          return !needsTypeConversion(op.getSource().getType(),
                                      typeConverter) &&
                 !needsTypeConversion(op.getStart().getType(),
                                      typeConverter) &&
                 !needsTypeConversion(op.getLength().getType(),
                                      typeConverter) &&
                 !needsTypeConversion(op.getResult().getType(), typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::TypedReturnOp>(
        [&](rustmir::TypedReturnOp op) {
          return llvm::none_of(op.getValues(), [&](Value value) {
            return needsTypeConversion(value.getType(), typeConverter);
          });
        });
    target.addDynamicallyLegalOp<rustmir::TypedSwitchIntOp>(
        [&](rustmir::TypedSwitchIntOp op) {
          return !needsTypeConversion(op.getDiscr().getType(), typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::TypedAssertOp>(
        [&](rustmir::TypedAssertOp op) {
          return !needsTypeConversion(op.getCond().getType(), typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::TypedCallOp>(
        [&](rustmir::TypedCallOp op) {
          return llvm::none_of(op.getArgs(),
                               [&](Value value) {
                                 return needsTypeConversion(value.getType(),
                                                            typeConverter);
                               }) &&
                 llvm::none_of(op.getResults(), [&](Value value) {
                   return needsTypeConversion(value.getType(), typeConverter);
                 });
        });
    target.markUnknownOpDynamicallyLegal([](Operation *) { return true; });

    RewritePatternSet patterns(context);
    patterns.add<
        TypedConstOpConversion,
        SimpleBinaryOpConversion<rustmir::AddOp, arith::AddIOp>,
        SimpleBinaryOpConversion<rustmir::AddUncheckedOp, arith::AddIOp>,
        SimpleBinaryOpConversion<rustmir::SubOp, arith::SubIOp>,
        SimpleBinaryOpConversion<rustmir::SubUncheckedOp, arith::SubIOp>,
        SimpleBinaryOpConversion<rustmir::MulOp, arith::MulIOp>,
        SimpleBinaryOpConversion<rustmir::MulUncheckedOp, arith::MulIOp>,
        DivOpConversion<rustmir::DivOp>, RemOpConversion<rustmir::RemOp>,
        SimpleBinaryOpConversion<rustmir::BitAndOp, arith::AndIOp>,
        SimpleBinaryOpConversion<rustmir::BitOrOp, arith::OrIOp>,
        SimpleBinaryOpConversion<rustmir::BitXorOp, arith::XOrIOp>,
        SimpleBinaryOpConversion<rustmir::ShlOp, arith::ShLIOp>,
        SimpleBinaryOpConversion<rustmir::ShlUncheckedOp, arith::ShLIOp>,
        ShrOpConversion<rustmir::ShrOp>,
        ShrOpConversion<rustmir::ShrUncheckedOp>,
        CompareOpConversion<rustmir::EqOp, arith::CmpIPredicate::eq,
                            arith::CmpIPredicate::eq>,
        CompareOpConversion<rustmir::NeOp, arith::CmpIPredicate::ne,
                            arith::CmpIPredicate::ne>,
        CompareOpConversion<rustmir::LtOp, arith::CmpIPredicate::slt,
                            arith::CmpIPredicate::ult>,
        CompareOpConversion<rustmir::LeOp, arith::CmpIPredicate::sle,
                            arith::CmpIPredicate::ule>,
        CompareOpConversion<rustmir::GtOp, arith::CmpIPredicate::sgt,
                            arith::CmpIPredicate::ugt>,
        CompareOpConversion<rustmir::GeOp, arith::CmpIPredicate::sge,
                            arith::CmpIPredicate::uge>,
        CheckedAddOpConversion, CheckedSubOpConversion, CheckedMulOpConversion,
        NegOpConversion, NotOpConversion, LocalSlotConversion, LoadConversion,
        StoreConversion, BorrowOpConversion, RawAddressOpConversion,
        MakeAggregateConversion, FieldConversion, FieldAddrConversion,
        IndexAddrConversion, SliceFromArrayConversion, PtrMetadataConversion,
        SubsliceConversion, SliceRangeConversion, TypedReturnConversion,
        TypedSwitchIntConversion, TypedAssertConversion, TypedCallConversion>(
        typeConverter, context);
    if (failed(applyPartialConversion(module, target, std::move(patterns))))
      signalPassFailure();
  }
};
} // namespace

std::unique_ptr<Pass> mlir::createConvertRustTypedToArithPass() {
  return std::make_unique<ConvertRustTypedToArithPass>();
}
