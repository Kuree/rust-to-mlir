//===- RustTypedToArith.cpp - Rust typed to arith conversion ---*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Conversion/RustTypedToArith/RustTypedToArith.h"

#include "RustToLLVM/Support/OpCreateCompat.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustTypes.h"
#include "mlir/Dialect/RustTyped/IR/RustTypedOps.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringSwitch.h"

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

std::optional<unsigned> getFixedIntegerWidth(StringRef spelling,
                                             unsigned pointerWidth) {
  if (spelling == "isize" || spelling == "usize")
    return pointerWidth;

  if (!spelling.consume_front("i") && !spelling.consume_front("u"))
    return std::nullopt;

  unsigned width = 0;
  if (spelling.getAsInteger(10, width) || width == 0)
    return std::nullopt;
  return width;
}

bool isSignedRustInteger(Type type) {
  auto intType = dyn_cast<rustmir::IntType>(type);
  if (!intType)
    return true;
  StringRef spelling = intType.getSpelling();
  return spelling == "isize" || spelling.starts_with("i");
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
      std::optional<unsigned> width =
          getFixedIntegerWidth(type.getSpelling(), this->pointerWidth);
      if (!width)
        return Type();
      return IntegerType::get(this->context, *width);
    });
    addConversion([this](rustmir::SlotType type) -> Type {
      Type elementType = convertType(type.getElementType());
      if (!elementType)
        return Type();
      return rustmir::SlotType::get(this->context, elementType);
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
      return rustmir::TypedTupleType::get(this->context,
                                          ArrayRef<Type>(elementTypes));
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

bool isSupportedBinOp(StringRef op) {
  return llvm::StringSwitch<bool>(op)
      .Cases("Add", "AddUnchecked", "Sub", "SubUnchecked", true)
      .Cases("Mul", "MulUnchecked", "Div", "Rem", true)
      .Cases("BitAnd", "BitOr", "BitXor", true)
      .Cases("Shl", "ShlUnchecked", "Shr", "ShrUnchecked", true)
      .Cases("Eq", "Ne", "Lt", "Le", "Gt", "Ge", true)
      .Default(false);
}

bool isSupportedUnOp(StringRef op) {
  return llvm::StringSwitch<bool>(op).Cases("Not", "Neg", true).Default(false);
}

bool isLowerableBinOp(rustmir::BinOp op, const TypeConverter &converter) {
  return isSupportedBinOp(op.getOp()) &&
         isIntegerLikeAfterConversion(op.getLhs().getType(), converter) &&
         isIntegerLikeAfterConversion(op.getRhs().getType(), converter) &&
         isIntegerLikeAfterConversion(op.getResult().getType(), converter);
}

bool isLowerableUnOp(rustmir::UnOp op, const TypeConverter &converter) {
  return isSupportedUnOp(op.getOp()) &&
         isIntegerLikeAfterConversion(op.getInput().getType(), converter) &&
         isIntegerLikeAfterConversion(op.getResult().getType(), converter);
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

struct BinOpConversion : public OpConversionPattern<rustmir::BinOp> {
  using OpConversionPattern<rustmir::BinOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::BinOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type convertedType =
        getTypeConverter()->convertType(op.getResult().getType());
    if (!isa_and_nonnull<IntegerType>(convertedType))
      return failure();

    Value lhs = adaptor.getLhs();
    Value rhs = adaptor.getRhs();
    StringRef kind = op.getOp();
    bool isSigned = isSignedRustInteger(op.getLhs().getType());
    Value replacement;

    if (kind == "Add" || kind == "AddUnchecked")
      replacement =
          mlir::rust::createOp<arith::AddIOp>(rewriter, op.getLoc(), lhs, rhs)
              .getResult();
    else if (kind == "Sub" || kind == "SubUnchecked")
      replacement =
          mlir::rust::createOp<arith::SubIOp>(rewriter, op.getLoc(), lhs, rhs)
              .getResult();
    else if (kind == "Mul" || kind == "MulUnchecked")
      replacement =
          mlir::rust::createOp<arith::MulIOp>(rewriter, op.getLoc(), lhs, rhs)
              .getResult();
    else if (kind == "Div")
      replacement = isSigned ? mlir::rust::createOp<arith::DivSIOp>(
                                   rewriter, op.getLoc(), lhs, rhs)
                                   .getResult()
                             : mlir::rust::createOp<arith::DivUIOp>(
                                   rewriter, op.getLoc(), lhs, rhs)
                                   .getResult();
    else if (kind == "Rem")
      replacement = isSigned ? mlir::rust::createOp<arith::RemSIOp>(
                                   rewriter, op.getLoc(), lhs, rhs)
                                   .getResult()
                             : mlir::rust::createOp<arith::RemUIOp>(
                                   rewriter, op.getLoc(), lhs, rhs)
                                   .getResult();
    else if (kind == "BitAnd")
      replacement =
          mlir::rust::createOp<arith::AndIOp>(rewriter, op.getLoc(), lhs, rhs)
              .getResult();
    else if (kind == "BitOr")
      replacement =
          mlir::rust::createOp<arith::OrIOp>(rewriter, op.getLoc(), lhs, rhs)
              .getResult();
    else if (kind == "BitXor")
      replacement =
          mlir::rust::createOp<arith::XOrIOp>(rewriter, op.getLoc(), lhs, rhs)
              .getResult();
    else if (kind == "Shl" || kind == "ShlUnchecked")
      replacement =
          mlir::rust::createOp<arith::ShLIOp>(rewriter, op.getLoc(), lhs, rhs)
              .getResult();
    else if (kind == "Shr" || kind == "ShrUnchecked")
      replacement = isSigned ? mlir::rust::createOp<arith::ShRSIOp>(
                                   rewriter, op.getLoc(), lhs, rhs)
                                   .getResult()
                             : mlir::rust::createOp<arith::ShRUIOp>(
                                   rewriter, op.getLoc(), lhs, rhs)
                                   .getResult();
    else {
      std::optional<arith::CmpIPredicate> predicate =
          llvm::StringSwitch<std::optional<arith::CmpIPredicate>>(kind)
              .Case("Eq", arith::CmpIPredicate::eq)
              .Case("Ne", arith::CmpIPredicate::ne)
              .Case("Lt", isSigned ? arith::CmpIPredicate::slt
                                   : arith::CmpIPredicate::ult)
              .Case("Le", isSigned ? arith::CmpIPredicate::sle
                                   : arith::CmpIPredicate::ule)
              .Case("Gt", isSigned ? arith::CmpIPredicate::sgt
                                   : arith::CmpIPredicate::ugt)
              .Case("Ge", isSigned ? arith::CmpIPredicate::sge
                                   : arith::CmpIPredicate::uge)
              .Default(std::nullopt);
      if (!predicate)
        return failure();
      replacement = mlir::rust::createOp<arith::CmpIOp>(rewriter, op.getLoc(),
                                                        *predicate, lhs, rhs)
                        .getResult();
    }

    rewriter.replaceOp(op, replacement);
    return success();
  }
};

struct UnOpConversion : public OpConversionPattern<rustmir::UnOp> {
  using OpConversionPattern<rustmir::UnOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::UnOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    auto resultType = dyn_cast_or_null<IntegerType>(
        getTypeConverter()->convertType(op.getResult().getType()));
    if (!resultType)
      return failure();

    Value input = adaptor.getInput();
    Value replacement;
    if (op.getOp() == "Neg") {
      Value zero =
          createIntegerConstant(rewriter, op.getLoc(), resultType,
                                llvm::APInt::getZero(resultType.getWidth()));
      replacement = mlir::rust::createOp<arith::SubIOp>(rewriter, op.getLoc(),
                                                        zero, input)
                        .getResult();
    } else if (op.getOp() == "Not") {
      Value allOnes =
          createIntegerConstant(rewriter, op.getLoc(), resultType,
                                llvm::APInt::getAllOnes(resultType.getWidth()));
      replacement = mlir::rust::createOp<arith::XOrIOp>(rewriter, op.getLoc(),
                                                        input, allOnes)
                        .getResult();
    } else {
      return failure();
    }

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
        op.getMutabilityAttr(), op.getRoleAttr(), op.getSpanAttr());
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

struct MakeTupleConversion : public OpConversionPattern<rustmir::MakeTupleOp> {
  using OpConversionPattern<rustmir::MakeTupleOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::MakeTupleOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!resultType)
      return failure();
    auto newOp = mlir::rust::createOp<rustmir::MakeTupleOp>(
        rewriter, op.getLoc(), resultType, adaptor.getValues(),
        op.getSpanAttr());
    rewriter.replaceOp(op, newOp.getResult());
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
    auto newOp = mlir::rust::createOp<rustmir::FieldOp>(
        rewriter, op.getLoc(), resultType, adaptor.getAggregate(),
        op.getIndexAttr(), op.getSpanAttr());
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

struct ConvertRustTypedToArithPass
    : public mlir::impl::ConvertRustTypedToArithPassBase<
          ConvertRustTypedToArithPass> {
  void runOnOperation() final {
    ModuleOp module = getOperation();
    MLIRContext *context = module.getContext();
    RustScalarTypeConverter typeConverter(context, getPointerWidth(module));

    ConversionTarget target(*context);
    target.addLegalDialect<arith::ArithDialect, rustmir::RustMIRDialect,
                           ub::UBDialect>();
    target.addDynamicallyLegalOp<rustmir::TypedConstOp>(
        [&](rustmir::TypedConstOp op) {
          return !isLowerableConst(op, typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::BinOp>([&](rustmir::BinOp op) {
      return !isLowerableBinOp(op, typeConverter);
    });
    target.addDynamicallyLegalOp<rustmir::UnOp>(
        [&](rustmir::UnOp op) { return !isLowerableUnOp(op, typeConverter); });
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
    target.addDynamicallyLegalOp<rustmir::MakeTupleOp>(
        [&](rustmir::MakeTupleOp op) {
          return !needsTypeConversion(op.getResult().getType(), typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::FieldOp>([&](rustmir::FieldOp op) {
      return !needsTypeConversion(op.getAggregate().getType(), typeConverter) &&
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
    target.markUnknownOpDynamicallyLegal([](Operation *) { return true; });

    RewritePatternSet patterns(context);
    patterns.add<TypedConstOpConversion, BinOpConversion, UnOpConversion,
                 LocalSlotConversion, LoadConversion, StoreConversion,
                 MakeTupleConversion, FieldConversion, TypedReturnConversion,
                 TypedSwitchIntConversion>(typeConverter, context);
    if (failed(applyPartialConversion(module, target, std::move(patterns))))
      signalPassFailure();
  }
};
} // namespace

std::unique_ptr<Pass> mlir::createConvertRustTypedToArithPass() {
  return std::make_unique<ConvertRustTypedToArithPass>();
}
