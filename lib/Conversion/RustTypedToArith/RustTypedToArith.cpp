//===- RustTypedToArith.cpp - Rust typed to arith conversion ---*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Conversion/RustTypedToArith/RustTypedToArith.h"

#include "RustToMLIR/Support/OpCreateCompat.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Math/IR/Math.h"
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
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Error.h"

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
    addConversion([this](rustmir::FloatType type) -> Type {
      if (type.getBitWidth() == 32)
        return Float32Type::get(this->context);
      if (type.getBitWidth() == 64)
        return Float64Type::get(this->context);
      return Type();
    });
    addConversion([this](FunctionType type) -> Type {
      SmallVector<Type> inputs;
      SmallVector<Type> results;
      if (failed(convertTypes(type.getInputs(), inputs)) ||
          failed(convertTypes(type.getResults(), results)))
        return Type();
      return FunctionType::get(this->context, inputs, results);
    });
    addConversion([this](rustmir::UnitType) -> Type {
      return LLVM::LLVMStructType::getLiteral(this->context, {});
    });
    addConversion([this](rustmir::NeverType) -> Type {
      return LLVM::LLVMStructType::getLiteral(this->context, {});
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
      return convertAdtType(type);
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
  Type convertAdtType(rustmir::AdtType type) {
    ArrayRef<Type> variants = type.getVariants();
    if (variants.empty())
      return Type();
    if (variants.size() == 1)
      return convertType(variants.front());

    SmallVector<Type> elementTypes;
    elementTypes.reserve(variants.size() + 1);
    elementTypes.push_back(IntegerType::get(this->context, pointerWidth));
    for (Type variant : variants) {
      Type converted = convertType(variant);
      if (!converted)
        return Type();
      if (!isa<LLVM::LLVMStructType>(converted))
        return Type();
      elementTypes.push_back(converted);
    }
    return LLVM::LLVMStructType::getLiteral(this->context, elementTypes);
  }

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

bool isFloatLikeAfterConversion(Type type, const TypeConverter &converter) {
  Type converted = converter.convertType(type);
  return converted && isa<FloatType>(converted);
}

bool isLowerableConst(rustmir::TypedConstOp op,
                      const TypeConverter &converter) {
  return isIntegerLikeAfterConversion(op.getResult().getType(), converter) ||
         isFloatLikeAfterConversion(op.getResult().getType(), converter);
}

template <typename OpT>
bool isLowerableIntegerBinaryOp(OpT op, const TypeConverter &converter) {
  Type lhsType = converter.convertType(op.getLhs().getType());
  Type rhsType = converter.convertType(op.getRhs().getType());
  Type resultType = converter.convertType(op.getResult().getType());
  return isa_and_nonnull<IntegerType>(lhsType) && lhsType == rhsType &&
         lhsType == resultType;
}

template <typename OpT>
bool isLowerableFloatBinaryOp(OpT op, const TypeConverter &converter) {
  Type lhsType = converter.convertType(op.getLhs().getType());
  Type rhsType = converter.convertType(op.getRhs().getType());
  Type resultType = converter.convertType(op.getResult().getType());
  return isa_and_nonnull<FloatType>(lhsType) && lhsType == rhsType &&
         lhsType == resultType;
}

template <typename OpT>
bool isLowerableIntegerCompareOp(OpT op, const TypeConverter &converter) {
  Type lhsType = converter.convertType(op.getLhs().getType());
  Type rhsType = converter.convertType(op.getRhs().getType());
  Type resultType = converter.convertType(op.getResult().getType());
  return isa_and_nonnull<IntegerType>(lhsType) && lhsType == rhsType &&
         resultType && resultType.isSignlessInteger(1);
}

template <typename OpT>
bool isLowerableFloatCompareOp(OpT op, const TypeConverter &converter) {
  Type lhsType = converter.convertType(op.getLhs().getType());
  Type rhsType = converter.convertType(op.getRhs().getType());
  Type resultType = converter.convertType(op.getResult().getType());
  return isa_and_nonnull<FloatType>(lhsType) && lhsType == rhsType &&
         resultType && resultType.isSignlessInteger(1);
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
bool isLowerableIntegerUnaryOp(OpT op, const TypeConverter &converter) {
  Type inputType = converter.convertType(op.getInput().getType());
  Type resultType = converter.convertType(op.getResult().getType());
  return isa_and_nonnull<IntegerType>(inputType) && inputType == resultType;
}

template <typename OpT>
bool isLowerableFloatUnaryOp(OpT op, const TypeConverter &converter) {
  Type inputType = converter.convertType(op.getInput().getType());
  Type resultType = converter.convertType(op.getResult().getType());
  return isa_and_nonnull<FloatType>(inputType) && inputType == resultType;
}

bool isLowerableIntCast(rustmir::IntCastOp op,
                        const TypeConverter &converter) {
  Type inputType = converter.convertType(op.getInput().getType());
  Type resultType = converter.convertType(op.getResult().getType());
  return isa_and_nonnull<IntegerType>(inputType) &&
         isa_and_nonnull<IntegerType>(resultType);
}

bool isLowerableNumericCast(rustmir::NumericCastOp op,
                            const TypeConverter &converter) {
  Type inputType = converter.convertType(op.getInput().getType());
  Type resultType = converter.convertType(op.getResult().getType());
  switch (op.getCastKind()) {
  case rustmir::RustCastKind::FloatToInt:
    return isa_and_nonnull<FloatType>(inputType) &&
           isa_and_nonnull<IntegerType>(resultType);
  case rustmir::RustCastKind::FloatToFloat:
    return isa_and_nonnull<FloatType>(inputType) &&
           isa_and_nonnull<FloatType>(resultType);
  case rustmir::RustCastKind::IntToFloat:
    return isa_and_nonnull<IntegerType>(inputType) &&
           isa_and_nonnull<FloatType>(resultType);
  default:
    return false;
  }
}

enum class FloatMathMethod {
  Unsupported,
  Abs,
  Acos,
  Acosh,
  Asin,
  Asinh,
  Atan,
  Atan2,
  Atanh,
  Cbrt,
  Ceil,
  Copysign,
  Cos,
  Cosh,
  Erf,
  Exp,
  Exp2,
  ExpM1,
  Floor,
  Ln,
  Ln1p,
  Log10,
  Log2,
  MulAdd,
  Powf,
  Powi,
  Round,
  RoundTiesEven,
  Sin,
  Sinh,
  Sqrt,
  Tan,
  Tanh,
  Trunc,
};

std::optional<StringRef> getRustFloatMethodName(StringRef rustName) {
  constexpr StringLiteral prefixes[] = {
      "core::f32::<impl f32>::", "core::f64::<impl f64>::",
      "std::f32::<impl f32>::", "std::f64::<impl f64>::"};
  for (StringRef prefix : prefixes) {
    StringRef method = rustName;
    if (method.consume_front(prefix))
      return method;
  }
  return std::nullopt;
}

FloatMathMethod classifyFloatMathMethod(StringRef method) {
  return llvm::StringSwitch<FloatMathMethod>(method)
      .Case("abs", FloatMathMethod::Abs)
      .Case("acos", FloatMathMethod::Acos)
      .Case("acosh", FloatMathMethod::Acosh)
      .Case("asin", FloatMathMethod::Asin)
      .Case("asinh", FloatMathMethod::Asinh)
      .Case("atan", FloatMathMethod::Atan)
      .Case("atan2", FloatMathMethod::Atan2)
      .Case("atanh", FloatMathMethod::Atanh)
      .Case("cbrt", FloatMathMethod::Cbrt)
      .Case("ceil", FloatMathMethod::Ceil)
      .Case("copysign", FloatMathMethod::Copysign)
      .Case("cos", FloatMathMethod::Cos)
      .Case("cosh", FloatMathMethod::Cosh)
      .Case("erf", FloatMathMethod::Erf)
      .Case("exp", FloatMathMethod::Exp)
      .Case("exp2", FloatMathMethod::Exp2)
      .Case("exp_m1", FloatMathMethod::ExpM1)
      .Case("floor", FloatMathMethod::Floor)
      .Case("ln", FloatMathMethod::Ln)
      .Case("ln_1p", FloatMathMethod::Ln1p)
      .Case("log10", FloatMathMethod::Log10)
      .Case("log2", FloatMathMethod::Log2)
      .Case("mul_add", FloatMathMethod::MulAdd)
      .Case("powf", FloatMathMethod::Powf)
      .Case("powi", FloatMathMethod::Powi)
      .Case("round", FloatMathMethod::Round)
      .Case("round_ties_even", FloatMathMethod::RoundTiesEven)
      .Case("sin", FloatMathMethod::Sin)
      .Case("sinh", FloatMathMethod::Sinh)
      .Case("sqrt", FloatMathMethod::Sqrt)
      .Case("tan", FloatMathMethod::Tan)
      .Case("tanh", FloatMathMethod::Tanh)
      .Case("trunc", FloatMathMethod::Trunc)
      .Default(FloatMathMethod::Unsupported);
}

std::optional<FloatMathMethod> getFloatMathMethod(rustmir::TypedCallOp op) {
  StringAttr rustName = op.getRustNameAttr();
  if (!rustName)
    rustName = op.getCalleeDefAttr();
  if (!rustName)
    return std::nullopt;

  std::optional<StringRef> methodName =
      getRustFloatMethodName(rustName.getValue());
  if (!methodName)
    return std::nullopt;

  FloatMathMethod method = classifyFloatMathMethod(*methodName);
  if (method == FloatMathMethod::Unsupported)
    return std::nullopt;
  return method;
}

bool isLowerableMakeAggregate(rustmir::MakeAggregateOp op,
                              const TypeConverter &converter) {
  if (auto adtType = dyn_cast<rustmir::AdtType>(op.getResult().getType())) {
    ArrayRef<Type> variants = adtType.getVariants();
    if (variants.size() > 1) {
      IntegerAttr variantIndexAttr = op.getVariantIndexAttr();
      if (!variantIndexAttr || variantIndexAttr.getInt() < 0 ||
          static_cast<size_t>(variantIndexAttr.getInt()) >= variants.size())
        return false;
      Type variant = variants[variantIndexAttr.getInt()];
      if (isa<rustmir::UnitType>(variant))
        return op.getValues().empty() &&
               isa_and_nonnull<LLVM::LLVMStructType>(
                   converter.convertType(op.getResult().getType()));
      auto tupleType = dyn_cast<rustmir::TypedTupleType>(variant);
      return tupleType && tupleType.getElementTypes().size() ==
                              op.getValues().size() &&
             isa_and_nonnull<LLVM::LLVMStructType>(
                 converter.convertType(op.getResult().getType()));
    }
  }

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
  if (IntegerAttr variantIndexAttr = op.getVariantIndexAttr()) {
    auto structType = dyn_cast_or_null<LLVM::LLVMStructType>(aggregateType);
    int64_t variantIndex = variantIndexAttr.getInt();
    if (!structType || variantIndex < 0 || index < 0)
      return false;
    size_t payloadPosition = static_cast<size_t>(variantIndex) + 1;
    if (payloadPosition >= structType.getBody().size())
      return false;
    auto payloadType =
        dyn_cast<LLVM::LLVMStructType>(structType.getBody()[payloadPosition]);
    return payloadType &&
           static_cast<size_t>(index) < payloadType.getBody().size();
  }
  if (auto structType = dyn_cast_or_null<LLVM::LLVMStructType>(aggregateType))
    return index >= 0 &&
           static_cast<size_t>(index) < structType.getBody().size();
  if (auto arrayType = dyn_cast_or_null<LLVM::LLVMArrayType>(aggregateType))
    return index >= 0 &&
           static_cast<uint64_t>(index) < arrayType.getNumElements();
  return false;
}

bool isLowerableDiscriminant(rustmir::TypedDiscriminantOp op,
                             const TypeConverter &converter) {
  auto adtType = dyn_cast<rustmir::AdtType>(op.getAggregate().getType());
  if (!adtType || adtType.getVariants().empty())
    return false;

  Type aggregateType = converter.convertType(op.getAggregate().getType());
  Type resultType = converter.convertType(op.getResult().getType());
  return aggregateType && isa_and_nonnull<IntegerType>(resultType);
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

std::optional<std::string> parseFloatLiteralText(StringRef text) {
  text = text.trim();
  if (text.consume_front("const"))
    text = text.trim();

  size_t space = text.find_first_of(" \t\r\n");
  if (space != StringRef::npos)
    text = text.take_front(space);

  std::string normalized;
  normalized.reserve(text.size());
  for (char c : text)
    if (c != '_')
      normalized.push_back(c);

  text = StringRef(normalized).trim();
  if (text.consume_back("f16") || text.consume_back("f32") ||
      text.consume_back("f64") || text.consume_back("f128"))
    text = text.trim();

  if (text.empty())
    return std::nullopt;
  return text.str();
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

std::optional<llvm::APFloat> buildFloatValue(rustmir::TypedConstOp op,
                                             FloatType resultType) {
  if (auto floatAttr = dyn_cast_or_null<FloatAttr>(op.getValueAttr())) {
    llvm::APFloat value = floatAttr.getValue();
    bool losesInfo = false;
    value.convert(resultType.getFloatSemantics(),
                  llvm::APFloat::rmNearestTiesToEven, &losesInfo);
    return value;
  }

  std::optional<StringRef> debug = op.getDebug();
  if (!debug)
    return {};

  std::optional<std::string> value = parseFloatLiteralText(*debug);
  if (!value)
    return {};
  llvm::APFloat apValue(resultType.getFloatSemantics());
  llvm::Expected<llvm::APFloat::opStatus> status =
      apValue.convertFromString(*value, llvm::APFloat::rmNearestTiesToEven);
  if (!status) {
    llvm::consumeError(status.takeError());
    return {};
  }
  return apValue;
}

Value createIntegerConstant(OpBuilder &builder, Location loc, IntegerType type,
                            const llvm::APInt &value) {
  auto attr = cast<TypedAttr>(IntegerAttr::get(type, value));
  return mlir::rust::createOp<arith::ConstantOp>(builder, loc, type, attr)
      .getResult();
}

Value castIntegerToType(OpBuilder &builder, Location loc, Value input,
                        IntegerType resultType, bool isSigned) {
  auto inputType = dyn_cast<IntegerType>(input.getType());
  if (!inputType)
    return {};

  unsigned inputWidth = inputType.getWidth();
  unsigned resultWidth = resultType.getWidth();
  if (inputWidth == resultWidth)
    return input;

  if (inputWidth < resultWidth) {
    return isSigned
               ? mlir::rust::createOp<arith::ExtSIOp>(builder, loc,
                                                       resultType, input)
                     .getResult()
               : mlir::rust::createOp<arith::ExtUIOp>(builder, loc,
                                                       resultType, input)
                     .getResult();
  }

  return mlir::rust::createOp<arith::TruncIOp>(builder, loc, resultType, input)
      .getResult();
}

Value castAggregateElementToType(OpBuilder &builder, Location loc, Value input,
                                 Type expectedType, Type rustInputType) {
  if (input.getType() == expectedType)
    return input;

  auto inputIntegerType = dyn_cast<IntegerType>(input.getType());
  auto expectedIntegerType = dyn_cast<IntegerType>(expectedType);
  if (!inputIntegerType || !expectedIntegerType)
    return {};

  return castIntegerToType(builder, loc, input, expectedIntegerType,
                           isSignedRustInteger(rustInputType));
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

Value createFloatPredicate(OpBuilder &builder, Location loc,
                           arith::CmpFPredicate predicate, Value lhs,
                           Value rhs) {
  return mlir::rust::createOp<arith::CmpFOp>(builder, loc, predicate, lhs, rhs)
      .getResult();
}

Value createSelect(OpBuilder &builder, Location loc, Value condition,
                   Value trueValue, Value falseValue) {
  return mlir::rust::createOp<arith::SelectOp>(
             builder, loc, condition, trueValue, falseValue)
      .getResult();
}

Value createFloatConstant(OpBuilder &builder, Location loc, FloatType type,
                          const llvm::APFloat &value) {
  auto attr = cast<TypedAttr>(FloatAttr::get(type, value));
  return mlir::rust::createOp<arith::ConstantOp>(builder, loc, type, attr)
      .getResult();
}

Value createIntegerToFloatConstant(OpBuilder &builder, Location loc,
                                   FloatType resultType,
                                   const llvm::APInt &value, bool isSigned) {
  llvm::APFloat floatValue =
      llvm::APFloat::getZero(resultType.getFloatSemantics());
  floatValue.convertFromAPInt(value, isSigned,
                              llvm::APFloat::rmNearestTiesToEven);
  return createFloatConstant(builder, loc, resultType, floatValue);
}

Value createFloatZero(OpBuilder &builder, Location loc, FloatType resultType) {
  return createFloatConstant(
      builder, loc, resultType,
      llvm::APFloat::getZero(resultType.getFloatSemantics()));
}

Value createUnsignedPowerOfTwoFloat(OpBuilder &builder, Location loc,
                                    FloatType resultType, unsigned exponent) {
  llvm::APInt value(exponent + 1, 1);
  value <<= exponent;
  return createIntegerToFloatConstant(builder, loc, resultType, value,
                                      /*isSigned=*/false);
}

Value createSignedMinFloat(OpBuilder &builder, Location loc,
                           FloatType resultType, unsigned intWidth) {
  return createIntegerToFloatConstant(
      builder, loc, resultType, llvm::APInt::getSignedMinValue(intWidth),
      /*isSigned=*/true);
}

Value createSaturatingFloatToIntCast(OpBuilder &builder, Location loc,
                                     Value input, FloatType inputType,
                                     IntegerType resultType,
                                     Type rustResultType) {
  unsigned resultWidth = resultType.getWidth();
  bool isSigned = isSignedRustInteger(rustResultType);

  Value isNan = createFloatPredicate(builder, loc, arith::CmpFPredicate::UNO,
                                     input, input);
  Value zeroFloat = createFloatZero(builder, loc, inputType);
  Value zeroInt =
      createIntegerConstant(builder, loc, resultType,
                            llvm::APInt::getZero(resultWidth));

  Value tooLow;
  Value tooHigh;
  Value lowResult = zeroInt;
  Value highResult;

  if (isSigned) {
    Value minThreshold =
        createSignedMinFloat(builder, loc, inputType, resultWidth);
    Value maxThreshold = createUnsignedPowerOfTwoFloat(
        builder, loc, inputType, resultWidth - 1);
    tooLow = createFloatPredicate(builder, loc, arith::CmpFPredicate::OLE,
                                  input, minThreshold);
    tooHigh = createFloatPredicate(builder, loc, arith::CmpFPredicate::OGE,
                                   input, maxThreshold);
    lowResult =
        createIntegerConstant(builder, loc, resultType,
                              llvm::APInt::getSignedMinValue(resultWidth));
    highResult =
        createIntegerConstant(builder, loc, resultType,
                              llvm::APInt::getSignedMaxValue(resultWidth));
  } else {
    Value maxThreshold = createUnsignedPowerOfTwoFloat(
        builder, loc, inputType, resultWidth);
    tooLow = createFloatPredicate(builder, loc, arith::CmpFPredicate::OLE,
                                  input, zeroFloat);
    tooHigh = createFloatPredicate(builder, loc, arith::CmpFPredicate::OGE,
                                   input, maxThreshold);
    highResult = createIntegerConstant(builder, loc, resultType,
                                       llvm::APInt::getMaxValue(resultWidth));
  }

  Value safeInput = createSelect(builder, loc, isNan, zeroFloat, input);
  safeInput = createSelect(builder, loc, tooLow, zeroFloat, safeInput);
  safeInput = createSelect(builder, loc, tooHigh, zeroFloat, safeInput);

  Value castValue =
      isSigned ? mlir::rust::createOp<arith::FPToSIOp>(
                     builder, loc, resultType, safeInput)
                     .getResult()
               : mlir::rust::createOp<arith::FPToUIOp>(
                     builder, loc, resultType, safeInput)
                     .getResult();
  Value result = createSelect(builder, loc, tooLow, lowResult, castValue);
  result = createSelect(builder, loc, tooHigh, highResult, result);
  return createSelect(builder, loc, isNan, zeroInt, result);
}

struct TypedConstOpConversion
    : public OpConversionPattern<rustmir::TypedConstOp> {
  using OpConversionPattern<rustmir::TypedConstOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::TypedConstOp op, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type convertedType =
        getTypeConverter()->convertType(op.getResult().getType());
    auto integerType = dyn_cast_or_null<IntegerType>(convertedType);
    auto floatType = dyn_cast_or_null<FloatType>(convertedType);
    if (!integerType && !floatType)
      return failure();

    if (op.getDebug() && op.getDebug()->trim() == "uninit") {
      Value poison = mlir::rust::createOp<ub::PoisonOp>(
                         rewriter, op.getLoc(), convertedType,
                         ub::PoisonAttr::get(rewriter.getContext()))
                         .getResult();
      rewriter.replaceOp(op, poison);
      return success();
    }

    Value constant;
    if (integerType) {
      TypedAttr value = buildIntegerAttr(op, integerType);
      if (!value)
        return op.emitError("expected an integer or boolean rust.typed.const");
      constant = mlir::rust::createOp<arith::ConstantOp>(
                     rewriter, op.getLoc(), convertedType, value)
                     .getResult();
    } else {
      std::optional<llvm::APFloat> value = buildFloatValue(op, floatType);
      if (!value)
        return op.emitError("expected a floating-point rust.typed.const");
      auto attr = cast<TypedAttr>(FloatAttr::get(floatType, *value));
      constant = mlir::rust::createOp<arith::ConstantOp>(
                     rewriter, op.getLoc(), convertedType, attr)
                     .getResult();
    }
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
    if (!isa_and_nonnull<IntegerType>(
            this->getTypeConverter()->convertType(op.getResult().getType())))
      return failure();
    Value replacement =
        mlir::rust::createOp<TargetOp>(rewriter, op.getLoc(), adaptor.getLhs(),
                                       adaptor.getRhs())
            .getResult();
    rewriter.replaceOp(op, replacement);
    return success();
  }
};

template <typename SourceOp, typename TargetOp>
struct SimpleFloatBinaryOpConversion : public OpConversionPattern<SourceOp> {
  using OpConversionPattern<SourceOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(SourceOp op, typename SourceOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    if (!isa_and_nonnull<FloatType>(
            this->getTypeConverter()->convertType(op.getResult().getType())))
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
    if (!isa_and_nonnull<IntegerType>(
            this->getTypeConverter()->convertType(op.getResult().getType())))
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
    if (!isa_and_nonnull<IntegerType>(
            this->getTypeConverter()->convertType(op.getResult().getType())))
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
    if (!isa_and_nonnull<IntegerType>(
            this->getTypeConverter()->convertType(op.getResult().getType())))
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
    if (!isa_and_nonnull<IntegerType>(
            this->getTypeConverter()->convertType(op.getLhs().getType())) ||
        !isa_and_nonnull<IntegerType>(
            this->getTypeConverter()->convertType(op.getResult().getType())))
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

template <typename SourceOp, arith::CmpFPredicate predicate>
struct FloatCompareOpConversion : public OpConversionPattern<SourceOp> {
  using OpConversionPattern<SourceOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(SourceOp op, typename SourceOp::Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType =
        this->getTypeConverter()->convertType(op.getResult().getType());
    if (!isa_and_nonnull<FloatType>(
            this->getTypeConverter()->convertType(op.getLhs().getType())) ||
        !resultType || !resultType.isSignlessInteger(1))
      return failure();
    Value replacement =
        mlir::rust::createOp<arith::CmpFOp>(rewriter, op.getLoc(), predicate,
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

struct FloatNegOpConversion : public OpConversionPattern<rustmir::NegOp> {
  using OpConversionPattern<rustmir::NegOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::NegOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    if (!isa_and_nonnull<FloatType>(
            getTypeConverter()->convertType(op.getResult().getType())))
      return failure();

    Value replacement = mlir::rust::createOp<arith::NegFOp>(
                            rewriter, op.getLoc(), adaptor.getInput())
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

struct IntCastOpConversion : public OpConversionPattern<rustmir::IntCastOp> {
  using OpConversionPattern<rustmir::IntCastOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::IntCastOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    auto inputType = dyn_cast_or_null<IntegerType>(
        getTypeConverter()->convertType(op.getInput().getType()));
    auto resultType = dyn_cast_or_null<IntegerType>(
        getTypeConverter()->convertType(op.getResult().getType()));
    if (!inputType || !resultType)
      return failure();

    unsigned inputWidth = inputType.getWidth();
    unsigned resultWidth = resultType.getWidth();
    if (inputWidth == resultWidth) {
      rewriter.replaceOp(op, adaptor.getInput());
      return success();
    }

    Value replacement;
    if (inputWidth < resultWidth) {
      replacement =
          isSignedRustInteger(op.getInput().getType())
              ? mlir::rust::createOp<arith::ExtSIOp>(
                    rewriter, op.getLoc(), resultType, adaptor.getInput())
                    .getResult()
              : mlir::rust::createOp<arith::ExtUIOp>(
                    rewriter, op.getLoc(), resultType, adaptor.getInput())
                    .getResult();
    } else {
      replacement =
          mlir::rust::createOp<arith::TruncIOp>(
              rewriter, op.getLoc(), resultType, adaptor.getInput())
              .getResult();
    }

    rewriter.replaceOp(op, replacement);
    return success();
  }
};

struct NumericCastOpConversion
    : public OpConversionPattern<rustmir::NumericCastOp> {
  using OpConversionPattern<rustmir::NumericCastOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::NumericCastOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type inputType = getTypeConverter()->convertType(op.getInput().getType());
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!inputType || !resultType)
      return failure();

    switch (op.getCastKind()) {
    case rustmir::RustCastKind::FloatToInt: {
      auto sourceFloat = dyn_cast<FloatType>(inputType);
      auto resultInteger = dyn_cast<IntegerType>(resultType);
      if (!sourceFloat || !resultInteger)
        return failure();
      Value replacement = createSaturatingFloatToIntCast(
          rewriter, op.getLoc(), adaptor.getInput(), sourceFloat,
          resultInteger, op.getResult().getType());
      rewriter.replaceOp(op, replacement);
      return success();
    }
    case rustmir::RustCastKind::FloatToFloat: {
      auto sourceFloat = dyn_cast<FloatType>(inputType);
      auto resultFloat = dyn_cast<FloatType>(resultType);
      if (!sourceFloat || !resultFloat)
        return failure();

      unsigned sourceWidth = sourceFloat.getWidth();
      unsigned resultWidth = resultFloat.getWidth();
      if (sourceWidth == resultWidth) {
        rewriter.replaceOp(op, adaptor.getInput());
        return success();
      }

      Value replacement =
          sourceWidth < resultWidth
              ? mlir::rust::createOp<arith::ExtFOp>(
                    rewriter, op.getLoc(), resultType, adaptor.getInput())
                    .getResult()
              : mlir::rust::createOp<arith::TruncFOp>(
                    rewriter, op.getLoc(), resultType, adaptor.getInput())
                    .getResult();
      rewriter.replaceOp(op, replacement);
      return success();
    }
    case rustmir::RustCastKind::IntToFloat: {
      if (!isa<IntegerType>(inputType) || !isa<FloatType>(resultType))
        return failure();
      Value replacement =
          isSignedRustInteger(op.getInput().getType())
              ? mlir::rust::createOp<arith::SIToFPOp>(
                    rewriter, op.getLoc(), resultType, adaptor.getInput())
                    .getResult()
              : mlir::rust::createOp<arith::UIToFPOp>(
                    rewriter, op.getLoc(), resultType, adaptor.getInput())
                    .getResult();
      rewriter.replaceOp(op, replacement);
      return success();
    }
    default:
      return failure();
    }
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

struct TypedSetDiscriminantConversion
    : public OpConversionPattern<rustmir::TypedSetDiscriminantOp> {
  using OpConversionPattern<
      rustmir::TypedSetDiscriminantOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::TypedSetDiscriminantOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    mlir::rust::createOp<rustmir::TypedSetDiscriminantOp>(
        rewriter, op.getLoc(), adaptor.getBase(), op.getVariantIndexAttr(),
        op.getDiscriminantAttr(), op.getSpanAttr());
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

    if (auto adtType = dyn_cast<rustmir::AdtType>(op.getResult().getType())) {
      if (adtType.getVariants().size() > 1) {
        auto enumType = dyn_cast<LLVM::LLVMStructType>(resultType);
        IntegerAttr variantIndexAttr = op.getVariantIndexAttr();
        if (!enumType || !variantIndexAttr)
          return failure();

        int64_t variantIndex = variantIndexAttr.getInt();
        int64_t payloadPosition = variantIndex + 1;
        if (variantIndex < 0 || payloadPosition < 0 ||
            static_cast<size_t>(payloadPosition) >= enumType.getBody().size())
          return failure();
        auto payloadType =
            dyn_cast<LLVM::LLVMStructType>(enumType.getBody()[payloadPosition]);
        if (!payloadType ||
            payloadType.getBody().size() != adaptor.getValues().size())
          return failure();

        Value aggregate =
            mlir::rust::createOp<LLVM::UndefOp>(rewriter, op.getLoc(),
                                                resultType)
                .getRes();
        auto tagType = dyn_cast<IntegerType>(enumType.getBody().front());
        if (!tagType)
          return failure();
        llvm::APInt tagValue(tagType.getWidth(), variantIndex);
        if (StringAttr discriminant = op.getDiscriminantAttr()) {
          if (!discriminant.getValue().empty()) {
            std::optional<llvm::APInt> parsed =
                parseIntegerLiteral(discriminant.getValue(),
                                    tagType.getWidth());
            if (!parsed)
              return failure();
            tagValue = *parsed;
          }
        }

        Value tag = createIntegerConstant(rewriter, op.getLoc(), tagType,
                                          tagValue);
        aggregate = mlir::rust::createOp<LLVM::InsertValueOp>(
                        rewriter, op.getLoc(), aggregate, tag,
                        ArrayRef<int64_t>(0))
                        .getRes();

        Value payload =
            mlir::rust::createOp<LLVM::UndefOp>(rewriter, op.getLoc(),
                                                payloadType)
                .getRes();
        for (auto [index, value] : llvm::enumerate(adaptor.getValues())) {
          int64_t position = static_cast<int64_t>(index);
          value = castAggregateElementToType(
              rewriter, op.getLoc(), value, payloadType.getBody()[position],
              op.getValues()[index].getType());
          if (!value)
            return failure();
          payload = mlir::rust::createOp<LLVM::InsertValueOp>(
                        rewriter, op.getLoc(), payload, value,
                        ArrayRef<int64_t>(position))
                        .getRes();
        }
        aggregate = mlir::rust::createOp<LLVM::InsertValueOp>(
                        rewriter, op.getLoc(), aggregate, payload,
                        ArrayRef<int64_t>(payloadPosition))
                        .getRes();
        rewriter.replaceOp(op, aggregate);
        return success();
      }
    }

    Value aggregate =
        mlir::rust::createOp<LLVM::UndefOp>(rewriter, op.getLoc(), resultType)
            .getRes();
    for (auto [index, value] : llvm::enumerate(adaptor.getValues())) {
      int64_t position = static_cast<int64_t>(index);
      Type expectedElementType;
      if (auto structType = dyn_cast<LLVM::LLVMStructType>(resultType))
        expectedElementType = structType.getBody()[position];
      else if (auto arrayType = dyn_cast<LLVM::LLVMArrayType>(resultType))
        expectedElementType = arrayType.getElementType();
      else
        return failure();
      value = castAggregateElementToType(rewriter, op.getLoc(), value,
                                         expectedElementType,
                                         op.getValues()[index].getType());
      if (!value)
        return failure();
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
    SmallVector<int64_t> positionPath;
    if (IntegerAttr variantIndex = op.getVariantIndexAttr()) {
      positionPath.push_back(variantIndex.getInt() + 1);
      positionPath.push_back(position);
    } else {
      positionPath.push_back(position);
    }
    Value field = mlir::rust::createOp<LLVM::ExtractValueOp>(
                      rewriter, op.getLoc(), resultType, adaptor.getAggregate(),
                      ArrayRef<int64_t>(positionPath))
                      .getRes();
    rewriter.replaceOp(op, field);
    return success();
  }
};

struct DiscriminantConversion
    : public OpConversionPattern<rustmir::TypedDiscriminantOp> {
  using OpConversionPattern<rustmir::TypedDiscriminantOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::TypedDiscriminantOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    Type aggregateType =
        getTypeConverter()->convertType(op.getAggregate().getType());
    if (!resultType || !aggregateType)
      return failure();

    auto adtType = dyn_cast<rustmir::AdtType>(op.getAggregate().getType());
    if (!adtType || adtType.getVariants().empty())
      return failure();

    if (adtType.getVariants().size() == 1) {
      auto integerType = dyn_cast<IntegerType>(resultType);
      if (!integerType)
        return failure();
      Value zero = createIntegerConstant(
          rewriter, op.getLoc(), integerType,
          llvm::APInt::getZero(integerType.getWidth()));
      rewriter.replaceOp(op, zero);
      return success();
    }

    auto enumType = dyn_cast<LLVM::LLVMStructType>(aggregateType);
    auto resultIntegerType = dyn_cast<IntegerType>(resultType);
    if (!enumType || enumType.getBody().empty() || !resultIntegerType)
      return failure();
    auto tagType = dyn_cast<IntegerType>(enumType.getBody().front());
    if (!tagType)
      return failure();

    Value tag = mlir::rust::createOp<LLVM::ExtractValueOp>(
                    rewriter, op.getLoc(), tagType, adaptor.getAggregate(),
                    ArrayRef<int64_t>(0))
                    .getRes();
    Value discriminant =
        castIntegerToType(rewriter, op.getLoc(), tag, resultIntegerType,
                          isSignedRustInteger(op.getResult().getType()));
    if (!discriminant)
      return failure();
    rewriter.replaceOp(op, discriminant);
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
        op.getVariantIndexAttr(), op.getSpanAttr());
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

struct FloatMathCallConversion
    : public OpConversionPattern<rustmir::TypedCallOp> {
  using OpConversionPattern<rustmir::TypedCallOp>::OpConversionPattern;

  template <typename MathOp>
  LogicalResult rewriteUnary(rustmir::TypedCallOp op, ValueRange args,
                             Type resultType,
                             ConversionPatternRewriter &rewriter) const {
    if (args.size() != 1 || args.front().getType() != resultType)
      return rewriter.notifyMatchFailure(
          op, "expected one float argument matching the result type");
    Value replacement =
        mlir::rust::createOp<MathOp>(rewriter, op.getLoc(), resultType,
                                    args.front())
            .getResult();
    rewriter.replaceOp(op, replacement);
    return success();
  }

  template <typename MathOp>
  LogicalResult rewriteBinary(rustmir::TypedCallOp op, ValueRange args,
                              Type resultType,
                              ConversionPatternRewriter &rewriter) const {
    if (args.size() != 2 || args[0].getType() != resultType ||
        args[1].getType() != resultType)
      return rewriter.notifyMatchFailure(
          op, "expected two float arguments matching the result type");
    Value replacement =
        mlir::rust::createOp<MathOp>(rewriter, op.getLoc(), resultType, args[0],
                                    args[1])
            .getResult();
    rewriter.replaceOp(op, replacement);
    return success();
  }

  template <typename MathOp>
  LogicalResult rewriteTernary(rustmir::TypedCallOp op, ValueRange args,
                               Type resultType,
                               ConversionPatternRewriter &rewriter) const {
    if (args.size() != 3 || args[0].getType() != resultType ||
        args[1].getType() != resultType || args[2].getType() != resultType)
      return rewriter.notifyMatchFailure(
          op, "expected three float arguments matching the result type");
    Value replacement =
        mlir::rust::createOp<MathOp>(rewriter, op.getLoc(), resultType, args[0],
                                    args[1], args[2])
            .getResult();
    rewriter.replaceOp(op, replacement);
    return success();
  }

  LogicalResult rewriteFPowI(rustmir::TypedCallOp op, ValueRange args,
                             Type resultType,
                             ConversionPatternRewriter &rewriter) const {
    if (args.size() != 2 || args[0].getType() != resultType ||
        !isa<IntegerType>(args[1].getType()))
      return rewriter.notifyMatchFailure(
          op, "expected float base and integer exponent arguments");
    Value replacement =
        mlir::rust::createOp<math::FPowIOp>(rewriter, op.getLoc(), resultType,
                                           args[0], args[1])
            .getResult();
    rewriter.replaceOp(op, replacement);
    return success();
  }

  LogicalResult
  matchAndRewrite(rustmir::TypedCallOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    std::optional<FloatMathMethod> method = getFloatMathMethod(op);
    if (!method)
      return failure();

    SmallVector<Type> resultTypes;
    if (failed(
            getTypeConverter()->convertTypes(op.getResultTypes(), resultTypes)))
      return failure();
    if (resultTypes.size() != 1 || !isa<FloatType>(resultTypes.front()))
      return rewriter.notifyMatchFailure(op, "expected one float result");

    Type resultType = resultTypes.front();
    ValueRange args = adaptor.getArgs();
    switch (*method) {
    case FloatMathMethod::Abs:
      return rewriteUnary<math::AbsFOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Acos:
      return rewriteUnary<math::AcosOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Acosh:
      return rewriteUnary<math::AcoshOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Asin:
      return rewriteUnary<math::AsinOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Asinh:
      return rewriteUnary<math::AsinhOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Atan:
      return rewriteUnary<math::AtanOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Atan2:
      return rewriteBinary<math::Atan2Op>(op, args, resultType, rewriter);
    case FloatMathMethod::Atanh:
      return rewriteUnary<math::AtanhOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Cbrt:
      return rewriteUnary<math::CbrtOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Ceil:
      return rewriteUnary<math::CeilOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Copysign:
      return rewriteBinary<math::CopySignOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Cos:
      return rewriteUnary<math::CosOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Cosh:
      return rewriteUnary<math::CoshOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Erf:
      return rewriteUnary<math::ErfOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Exp:
      return rewriteUnary<math::ExpOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Exp2:
      return rewriteUnary<math::Exp2Op>(op, args, resultType, rewriter);
    case FloatMathMethod::ExpM1:
      return rewriteUnary<math::ExpM1Op>(op, args, resultType, rewriter);
    case FloatMathMethod::Floor:
      return rewriteUnary<math::FloorOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Ln:
      return rewriteUnary<math::LogOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Ln1p:
      return rewriteUnary<math::Log1pOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Log10:
      return rewriteUnary<math::Log10Op>(op, args, resultType, rewriter);
    case FloatMathMethod::Log2:
      return rewriteUnary<math::Log2Op>(op, args, resultType, rewriter);
    case FloatMathMethod::MulAdd:
      return rewriteTernary<math::FmaOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Powf:
      return rewriteBinary<math::PowFOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Powi:
      return rewriteFPowI(op, args, resultType, rewriter);
    case FloatMathMethod::Round:
      return rewriteUnary<math::RoundOp>(op, args, resultType, rewriter);
    case FloatMathMethod::RoundTiesEven:
      return rewriteUnary<math::RoundEvenOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Sin:
      return rewriteUnary<math::SinOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Sinh:
      return rewriteUnary<math::SinhOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Sqrt:
      return rewriteUnary<math::SqrtOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Tan:
      return rewriteUnary<math::TanOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Tanh:
      return rewriteUnary<math::TanhOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Trunc:
      return rewriteUnary<math::TruncOp>(op, args, resultType, rewriter);
    case FloatMathMethod::Unsupported:
      break;
    }
    return failure();
  }
};

struct TypedCallConversion : public OpConversionPattern<rustmir::TypedCallOp> {
  using OpConversionPattern<rustmir::TypedCallOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::TypedCallOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    if (getFloatMathMethod(op))
      return failure();

    SmallVector<Type> resultTypes;
    if (failed(
            getTypeConverter()->convertTypes(op.getResultTypes(), resultTypes)))
      return failure();

    auto converted = mlir::rust::createOp<rustmir::TypedCallOp>(
        rewriter, op.getLoc(), resultTypes, op.getCalleeAttr(),
        adaptor.getArgs(), op.getRustNameAttr(), op.getAbiAttr(),
        op.getCalleeDefAttr(), op.getCalleeTypeAttr(),
        op.getCalleeGenericArgsAttr(), op.getCalleeInputsAttr(),
        op.getCalleeOutputAttr(), op.getCalleeBridgeSymbolAttr(),
        op.getCVariadicAttr(), op.getTargetAttr(), op.getUnwindAttr(),
        op.getSpanAttr());
    for (NamedAttribute attr : op->getAttrs())
      if (!converted->hasAttr(attr.getName()))
        converted->setAttr(attr.getName(), attr.getValue());
    rewriter.replaceOp(op, converted->getResults());
    return success();
  }
};

struct TypedCallIndirectConversion
    : public OpConversionPattern<rustmir::TypedCallIndirectOp> {
  using OpConversionPattern<rustmir::TypedCallIndirectOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::TypedCallIndirectOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    SmallVector<Type> resultTypes;
    if (failed(
            getTypeConverter()->convertTypes(op.getResultTypes(), resultTypes)))
      return failure();

    auto converted = mlir::rust::createOp<rustmir::TypedCallIndirectOp>(
        rewriter, op.getLoc(), resultTypes, adaptor.getCallee(),
        adaptor.getArgs(), op.getTargetAttr(), op.getUnwindAttr(),
        op.getSpanAttr());
    for (NamedAttribute attr : op->getAttrs())
      if (!converted->hasAttr(attr.getName()))
        converted->setAttr(attr.getName(), attr.getValue());
    rewriter.replaceOp(op, converted->getResults());
    return success();
  }
};

struct FuncConstantConversion : public OpConversionPattern<func::ConstantOp> {
  using OpConversionPattern<func::ConstantOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(func::ConstantOp op, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!resultType)
      return failure();

    rewriter.replaceOpWithNewOp<func::ConstantOp>(op, resultType,
                                                  op.getValueAttr());
    return success();
  }
};

struct FnAddrConversion : public OpConversionPattern<rustmir::FnAddrOp> {
  using OpConversionPattern<rustmir::FnAddrOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::FnAddrOp op, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getResult().getType());
    if (!resultType)
      return failure();

    auto converted = mlir::rust::createOp<rustmir::FnAddrOp>(
        rewriter, op.getLoc(), resultType, op.getCalleeAttr(),
        op.getRustNameAttr(), op.getSpanAttr());
    for (NamedAttribute attr : op->getAttrs())
      if (!converted->hasAttr(attr.getName()))
        converted->setAttr(attr.getName(), attr.getValue());
    rewriter.replaceOp(op, converted.getResult());
    return success();
  }
};

template <typename OpT>
void addIntegerBinaryOpLegality(ConversionTarget &target,
                                const TypeConverter *typeConverter) {
  target.addDynamicallyLegalOp<OpT>([typeConverter](OpT op) {
    return !isLowerableIntegerBinaryOp(op, *typeConverter);
  });
}

template <typename OpT>
void addIntegerCompareOpLegality(ConversionTarget &target,
                                 const TypeConverter *typeConverter) {
  target.addDynamicallyLegalOp<OpT>([typeConverter](OpT op) {
    return !isLowerableIntegerCompareOp(op, *typeConverter);
  });
}

template <typename OpT>
void addIntegerOrFloatCompareOpLegality(ConversionTarget &target,
                                        const TypeConverter *typeConverter) {
  target.addDynamicallyLegalOp<OpT>([typeConverter](OpT op) {
    return !isLowerableIntegerCompareOp(op, *typeConverter) &&
           !isLowerableFloatCompareOp(op, *typeConverter);
  });
}

template <typename OpT>
void addIntegerOrFloatBinaryOpLegality(ConversionTarget &target,
                                       const TypeConverter *typeConverter) {
  target.addDynamicallyLegalOp<OpT>([typeConverter](OpT op) {
    return !isLowerableIntegerBinaryOp(op, *typeConverter) &&
           !isLowerableFloatBinaryOp(op, *typeConverter);
  });
}

template <typename OpT>
void addIntegerUnaryOpLegality(ConversionTarget &target,
                               const TypeConverter *typeConverter) {
  target.addDynamicallyLegalOp<OpT>([typeConverter](OpT op) {
    return !isLowerableIntegerUnaryOp(op, *typeConverter);
  });
}

template <typename OpT>
void addIntegerOrFloatUnaryOpLegality(ConversionTarget &target,
                                      const TypeConverter *typeConverter) {
  target.addDynamicallyLegalOp<OpT>([typeConverter](OpT op) {
    return !isLowerableIntegerUnaryOp(op, *typeConverter) &&
           !isLowerableFloatUnaryOp(op, *typeConverter);
  });
}

struct ConvertRustTypedToArithPass
    : public mlir::impl::ConvertRustTypedToArithPassBase<
          ConvertRustTypedToArithPass> {
  void getDependentDialects(DialectRegistry &registry) const final {
    registry
        .insert<arith::ArithDialect, LLVM::LLVMDialect, cf::ControlFlowDialect,
                func::FuncDialect, math::MathDialect, rustmir::RustMIRDialect,
                ub::UBDialect>();
  }

  void runOnOperation() final {
    ModuleOp module = getOperation();
    MLIRContext *context = module.getContext();
    RustScalarTypeConverter typeConverter(context, getPointerWidth(module));

    ConversionTarget target(*context);
    target.addLegalDialect<arith::ArithDialect, LLVM::LLVMDialect,
                           cf::ControlFlowDialect, func::FuncDialect,
                           math::MathDialect, rustmir::RustMIRDialect,
                           ub::UBDialect>();
    const TypeConverter *typeConverterPtr = &typeConverter;
    target.addDynamicallyLegalOp<rustmir::TypedConstOp>(
        [&](rustmir::TypedConstOp op) {
          return !isLowerableConst(op, typeConverter);
        });
    target.addDynamicallyLegalOp<func::ConstantOp>(
        [&](func::ConstantOp op) {
          return !needsTypeConversion(op.getResult().getType(),
                                      typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::FnAddrOp>(
        [&](rustmir::FnAddrOp op) {
          return !needsTypeConversion(op.getResult().getType(),
                                      typeConverter);
        });
    addIntegerOrFloatBinaryOpLegality<rustmir::AddOp>(target,
                                                      typeConverterPtr);
    addIntegerOrFloatBinaryOpLegality<rustmir::AddUncheckedOp>(
        target, typeConverterPtr);
    addIntegerOrFloatBinaryOpLegality<rustmir::SubOp>(target,
                                                      typeConverterPtr);
    addIntegerOrFloatBinaryOpLegality<rustmir::SubUncheckedOp>(
        target, typeConverterPtr);
    addIntegerOrFloatBinaryOpLegality<rustmir::MulOp>(target,
                                                      typeConverterPtr);
    addIntegerOrFloatBinaryOpLegality<rustmir::MulUncheckedOp>(
        target, typeConverterPtr);
    addIntegerOrFloatBinaryOpLegality<rustmir::DivOp>(target,
                                                      typeConverterPtr);
    addIntegerOrFloatBinaryOpLegality<rustmir::RemOp>(target,
                                                      typeConverterPtr);
    addIntegerBinaryOpLegality<rustmir::BitAndOp>(target, typeConverterPtr);
    addIntegerBinaryOpLegality<rustmir::BitOrOp>(target, typeConverterPtr);
    addIntegerBinaryOpLegality<rustmir::BitXorOp>(target, typeConverterPtr);
    addIntegerBinaryOpLegality<rustmir::ShlOp>(target, typeConverterPtr);
    addIntegerBinaryOpLegality<rustmir::ShlUncheckedOp>(target,
                                                        typeConverterPtr);
    addIntegerBinaryOpLegality<rustmir::ShrOp>(target, typeConverterPtr);
    addIntegerBinaryOpLegality<rustmir::ShrUncheckedOp>(target,
                                                        typeConverterPtr);
    addIntegerOrFloatCompareOpLegality<rustmir::EqOp>(target,
                                                      typeConverterPtr);
    addIntegerOrFloatCompareOpLegality<rustmir::NeOp>(target,
                                                      typeConverterPtr);
    addIntegerOrFloatCompareOpLegality<rustmir::LtOp>(target,
                                                      typeConverterPtr);
    addIntegerOrFloatCompareOpLegality<rustmir::LeOp>(target,
                                                      typeConverterPtr);
    addIntegerOrFloatCompareOpLegality<rustmir::GtOp>(target,
                                                      typeConverterPtr);
    addIntegerOrFloatCompareOpLegality<rustmir::GeOp>(target,
                                                      typeConverterPtr);
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
    addIntegerOrFloatUnaryOpLegality<rustmir::NegOp>(target,
                                                     typeConverterPtr);
    addIntegerUnaryOpLegality<rustmir::NotOp>(target, typeConverterPtr);
    target.addDynamicallyLegalOp<rustmir::IntCastOp>(
        [&](rustmir::IntCastOp op) {
          return !isLowerableIntCast(op, typeConverter);
        });
    target.addDynamicallyLegalOp<rustmir::NumericCastOp>(
        [&](rustmir::NumericCastOp op) {
          return !isLowerableNumericCast(op, typeConverter);
        });
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
    target.addDynamicallyLegalOp<rustmir::TypedSetDiscriminantOp>(
        [&](rustmir::TypedSetDiscriminantOp op) {
          return !needsTypeConversion(op.getBase().getType(), typeConverter);
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
    target.addDynamicallyLegalOp<rustmir::TypedDiscriminantOp>(
        [&](rustmir::TypedDiscriminantOp op) {
          return !isLowerableDiscriminant(op, typeConverter);
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
    target.addDynamicallyLegalOp<rustmir::TypedCallIndirectOp>(
        [&](rustmir::TypedCallIndirectOp op) {
          return !needsTypeConversion(op.getCallee().getType(),
                                      typeConverter) &&
                 llvm::none_of(op.getArgs(),
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
        SimpleFloatBinaryOpConversion<rustmir::AddOp, arith::AddFOp>,
        SimpleFloatBinaryOpConversion<rustmir::AddUncheckedOp, arith::AddFOp>,
        SimpleFloatBinaryOpConversion<rustmir::SubOp, arith::SubFOp>,
        SimpleFloatBinaryOpConversion<rustmir::SubUncheckedOp, arith::SubFOp>,
        SimpleFloatBinaryOpConversion<rustmir::MulOp, arith::MulFOp>,
        SimpleFloatBinaryOpConversion<rustmir::MulUncheckedOp, arith::MulFOp>,
        SimpleFloatBinaryOpConversion<rustmir::DivOp, arith::DivFOp>,
        SimpleFloatBinaryOpConversion<rustmir::RemOp, arith::RemFOp>,
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
        FloatCompareOpConversion<rustmir::EqOp, arith::CmpFPredicate::OEQ>,
        FloatCompareOpConversion<rustmir::NeOp, arith::CmpFPredicate::UNE>,
        FloatCompareOpConversion<rustmir::LtOp, arith::CmpFPredicate::OLT>,
        FloatCompareOpConversion<rustmir::LeOp, arith::CmpFPredicate::OLE>,
        FloatCompareOpConversion<rustmir::GtOp, arith::CmpFPredicate::OGT>,
        FloatCompareOpConversion<rustmir::GeOp, arith::CmpFPredicate::OGE>,
        CheckedAddOpConversion, CheckedSubOpConversion, CheckedMulOpConversion,
        NegOpConversion, FloatNegOpConversion, NotOpConversion,
        IntCastOpConversion, NumericCastOpConversion,
        LocalSlotConversion, LoadConversion,
        StoreConversion, TypedSetDiscriminantConversion, BorrowOpConversion,
        RawAddressOpConversion,
        MakeAggregateConversion, FieldConversion, DiscriminantConversion,
        FieldAddrConversion, IndexAddrConversion, SliceFromArrayConversion,
        PtrMetadataConversion, SubsliceConversion, SliceRangeConversion,
        TypedReturnConversion,
        TypedSwitchIntConversion, TypedAssertConversion,
        FloatMathCallConversion, TypedCallConversion,
        TypedCallIndirectConversion, FuncConstantConversion, FnAddrConversion>(
        typeConverter, context);
    if (failed(applyPartialConversion(module, target, std::move(patterns))))
      signalPassFailure();
  }
};
} // namespace

std::unique_ptr<Pass> mlir::createConvertRustTypedToArithPass() {
  return std::make_unique<ConvertRustTypedToArithPass>();
}
