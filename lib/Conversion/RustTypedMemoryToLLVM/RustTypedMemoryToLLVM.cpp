//===- RustTypedMemoryToLLVM.cpp - Rust typed memory to LLVM ---*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Conversion/RustTypedMemoryToLLVM/RustTypedMemoryToLLVM.h"

#include "RustToLLVM/Support/OpCreateCompat.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Func/Transforms/FuncConversions.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustTyped/IR/RustTypedOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"

#include <limits>

namespace mlir {
#define GEN_PASS_DEF_CONVERTRUSTTYPEDMEMORYTOLLVMPASS
#include "mlir/Conversion/RustTypedMemoryToLLVM/RustTypedMemoryToLLVMPasses.h.inc"
} // namespace mlir

using namespace mlir;
namespace rustmir = mlir::rust::mir;

namespace {
class RustTypedMemoryTypeConverter : public TypeConverter {
public:
  explicit RustTypedMemoryTypeConverter(MLIRContext *context)
      : context(context) {
    addConversion([](Type type) { return type; });
    addConversion([this](rustmir::SlotType) -> Type {
      return LLVM::LLVMPointerType::get(this->context);
    });
    addConversion([this](rustmir::TypedAddrType) -> Type {
      return LLVM::LLVMPointerType::get(this->context);
    });
    addConversion([this](rustmir::TypedRefType) -> Type {
      return LLVM::LLVMPointerType::get(this->context);
    });
    addConversion([this](rustmir::TypedRawPtrType) -> Type {
      return LLVM::LLVMPointerType::get(this->context);
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
  }

private:
  MLIRContext *context;
};

bool needsTypeConversion(Type type, const TypeConverter &converter) {
  Type converted = converter.convertType(type);
  return converted && converted != type;
}

bool hasTypeConversion(TypeRange types, const TypeConverter &converter) {
  return llvm::any_of(
      types, [&](Type type) { return needsTypeConversion(type, converter); });
}

Type getAddressElementType(Type addressType) {
  if (auto slotType = dyn_cast<rustmir::SlotType>(addressType))
    return slotType.getElementType();
  if (auto addrType = dyn_cast<rustmir::TypedAddrType>(addressType))
    return addrType.getElementType();
  return {};
}

Value createI64One(OpBuilder &builder, Location loc) {
  return mlir::rust::createOp<LLVM::ConstantOp>(builder, loc,
                                                builder.getI64Type(), 1)
      .getRes();
}

bool isRustStrRefType(Type type) {
  auto refType = dyn_cast<rustmir::TypedRefType>(type);
  if (!refType)
    return false;
  auto pointee = dyn_cast<rustmir::OpaqueType>(refType.getPointeeType());
  return pointee && pointee.getSpelling().contains("RigidTy(Str)");
}

StringAttr getStringLiteralAttr(Value value) {
  auto constOp = value.getDefiningOp<rustmir::TypedConstOp>();
  if (!constOp || !isRustStrRefType(constOp.getResult().getType()))
    return {};
  return dyn_cast_or_null<StringAttr>(constOp.getValueAttr());
}

bool isStringLiteralConst(rustmir::TypedConstOp op) {
  return isRustStrRefType(op.getResult().getType()) &&
         isa_and_nonnull<StringAttr>(op.getValueAttr());
}

bool hasRustName(Operation *op, StringRef name) {
  auto attr = op->getAttrOfType<StringAttr>("rust.rust_name");
  return attr && attr.getValue() == name;
}

std::string getStringGlobalName(StringRef value) {
  uint64_t hash = 1469598103934665603ULL;
  for (unsigned char byte : value.bytes()) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return ("__rtl_str_" + Twine(value.size()) + "_" + llvm::utohexstr(hash))
      .str();
}

LLVM::GlobalOp getOrCreateStringGlobal(ModuleOp module, Location loc,
                                       StringRef value, OpBuilder &builder) {
  std::string name = getStringGlobalName(value);
  if (auto global = dyn_cast_or_null<LLVM::GlobalOp>(
          SymbolTable::lookupSymbolIn(module, name)))
    return global;

  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());
  auto arrayType = LLVM::LLVMArrayType::get(builder.getContext(),
                                            builder.getI8Type(), value.size());
  return mlir::rust::createOp<LLVM::GlobalOp>(
      builder, loc, arrayType, /*isConstant=*/true, LLVM::Linkage::Private,
      name, builder.getStringAttr(value));
}

bool isStrAsPtrCall(func::CallOp op) {
  return hasRustName(op, "core::str::<impl str>::as_ptr") &&
         op.getNumOperands() == 1 && op.getNumResults() == 1;
}

bool isStrLenCall(func::CallOp op) {
  return hasRustName(op, "core::str::<impl str>::len") &&
         op.getNumOperands() == 1 && op.getNumResults() == 1;
}

void foldStringLiteralCalls(ModuleOp module) {
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  SmallVector<func::CallOp> calls;
  module.walk([&](func::CallOp op) {
    if (isStrAsPtrCall(op) || isStrLenCall(op))
      calls.push_back(op);
  });

  for (func::CallOp op : calls) {
    StringAttr valueAttr = getStringLiteralAttr(op.getOperand(0));
    if (!valueAttr)
      continue;

    builder.setInsertionPoint(op);
    if (isStrAsPtrCall(op)) {
      LLVM::GlobalOp global =
          getOrCreateStringGlobal(module, op.getLoc(), valueAttr.getValue(),
                                  builder);
      Value address = mlir::rust::createOp<LLVM::AddressOfOp>(
                          builder, op.getLoc(),
                          LLVM::LLVMPointerType::get(context),
                          global.getSymName())
                          .getRes();
      op.getResult(0).replaceAllUsesWith(address);
      op.erase();
      continue;
    }

    auto intType = dyn_cast<IntegerType>(op.getResult(0).getType());
    if (!intType)
      continue;

    Value len = mlir::rust::createOp<LLVM::ConstantOp>(
                    builder, op.getLoc(), intType,
                    IntegerAttr::get(intType, valueAttr.getValue().size()))
                    .getRes();
    op.getResult(0).replaceAllUsesWith(len);
    op.erase();
  }

  bool hasRemainingStringHelperCall = false;
  module.walk([&](func::CallOp op) {
    hasRemainingStringHelperCall |= isStrAsPtrCall(op) || isStrLenCall(op);
  });
  if (!hasRemainingStringHelperCall) {
    SmallVector<func::FuncOp> unusedStringHelpers;
    module.walk([&](func::FuncOp op) {
      if (op.isDeclaration() &&
          (hasRustName(op.getOperation(), "core::str::<impl str>::as_ptr") ||
           hasRustName(op.getOperation(), "core::str::<impl str>::len")))
        unusedStringHelpers.push_back(op);
    });
    for (func::FuncOp op : unusedStringHelpers)
      op.erase();
  }

  SmallVector<rustmir::TypedConstOp> stringConstants;
  module.walk([&](rustmir::TypedConstOp op) {
    if (isStringLiteralConst(op) && op->use_empty())
      stringConstants.push_back(op);
  });
  for (rustmir::TypedConstOp op : stringConstants)
    op.erase();
}

struct LocalSlotOpConversion
    : public OpConversionPattern<rustmir::LocalSlotOp> {
  using OpConversionPattern<rustmir::LocalSlotOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::LocalSlotOp op, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type elementType = getTypeConverter()->convertType(
        op.getSlot().getType().getElementType());
    if (!elementType)
      return failure();

    auto ptrType = LLVM::LLVMPointerType::get(op.getContext());
    Value arraySize = createI64One(rewriter, op.getLoc());
    auto alloca = mlir::rust::createOp<LLVM::AllocaOp>(
        rewriter, op.getLoc(), ptrType, elementType, arraySize);
    rewriter.replaceOp(op, alloca.getRes());
    return success();
  }
};

struct FieldAddrOpConversion
    : public OpConversionPattern<rustmir::FieldAddrOp> {
  using OpConversionPattern<rustmir::FieldAddrOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::FieldAddrOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type baseElementType = getAddressElementType(op.getBase().getType());
    Type convertedBaseElementType =
        getTypeConverter()->convertType(baseElementType);
    Type resultType =
        getTypeConverter()->convertType(op.getAddress().getType());
    if (!convertedBaseElementType || !resultType)
      return failure();

    int64_t fieldIndex = static_cast<int64_t>(op.getIndex());
    if (fieldIndex < 0 || fieldIndex > std::numeric_limits<int32_t>::max())
      return op.emitError("field index cannot be represented as a GEP index");

    SmallVector<LLVM::GEPArg> indices = {
        LLVM::GEPArg(0), LLVM::GEPArg(static_cast<int32_t>(fieldIndex))};
    Value gep = mlir::rust::createOp<LLVM::GEPOp>(
                    rewriter, op.getLoc(), resultType, convertedBaseElementType,
                    adaptor.getBase(), indices)
                    .getRes();
    rewriter.replaceOp(op, gep);
    return success();
  }
};

struct LoadOpConversion : public OpConversionPattern<rustmir::LoadOp> {
  using OpConversionPattern<rustmir::LoadOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::LoadOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    Type resultType = getTypeConverter()->convertType(op.getValue().getType());
    if (!resultType)
      return failure();

    Value loaded = mlir::rust::createOp<LLVM::LoadOp>(
                       rewriter, op.getLoc(), resultType, adaptor.getSlot())
                       .getRes();
    rewriter.replaceOp(op, loaded);
    return success();
  }
};

struct StoreOpConversion : public OpConversionPattern<rustmir::StoreOp> {
  using OpConversionPattern<rustmir::StoreOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::StoreOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    mlir::rust::createOp<LLVM::StoreOp>(rewriter, op.getLoc(),
                                        adaptor.getValue(), adaptor.getSlot());
    rewriter.eraseOp(op);
    return success();
  }
};

struct BorrowOpConversion : public OpConversionPattern<rustmir::BorrowOp> {
  using OpConversionPattern<rustmir::BorrowOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::BorrowOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    rewriter.replaceOp(op, adaptor.getSlot());
    return success();
  }
};

struct RawAddressOpConversion
    : public OpConversionPattern<rustmir::RawAddressOp> {
  using OpConversionPattern<rustmir::RawAddressOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rustmir::RawAddressOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    rewriter.replaceOp(op, adaptor.getSlot());
    return success();
  }
};

struct ConvertRustTypedMemoryToLLVMPass
    : public mlir::impl::ConvertRustTypedMemoryToLLVMPassBase<
          ConvertRustTypedMemoryToLLVMPass> {
  void getDependentDialects(DialectRegistry &registry) const final {
    registry.insert<func::FuncDialect, LLVM::LLVMDialect,
                    rustmir::RustMIRDialect>();
  }

  void runOnOperation() final {
    ModuleOp module = getOperation();
    MLIRContext *context = module.getContext();
    RustTypedMemoryTypeConverter typeConverter(context);

    foldStringLiteralCalls(module);

    ConversionTarget target(*context);
    target.addLegalDialect<func::FuncDialect, LLVM::LLVMDialect,
                           rustmir::RustMIRDialect>();
    target.addIllegalOp<rustmir::LocalSlotOp, rustmir::LoadOp, rustmir::StoreOp,
                        rustmir::FieldAddrOp, rustmir::BorrowOp,
                        rustmir::RawAddressOp>();
    target.addDynamicallyLegalOp<func::FuncOp>([&](func::FuncOp op) {
      return !hasTypeConversion(op.getFunctionType().getInputs(),
                                typeConverter) &&
             !hasTypeConversion(op.getFunctionType().getResults(),
                                typeConverter);
    });
    target.addDynamicallyLegalOp<func::CallOp>([&](func::CallOp op) {
      return !hasTypeConversion(op.getOperandTypes(), typeConverter) &&
             !hasTypeConversion(op.getResultTypes(), typeConverter);
    });
    target.addDynamicallyLegalOp<func::ReturnOp>([&](func::ReturnOp op) {
      return !hasTypeConversion(op.getOperandTypes(), typeConverter);
    });
    target.addDynamicallyLegalOp<rustmir::TypedConstOp>(
        [](rustmir::TypedConstOp op) { return !isStringLiteralConst(op); });
    target.markUnknownOpDynamicallyLegal([](Operation *) { return true; });

    RewritePatternSet patterns(context);
    patterns
        .add<LocalSlotOpConversion, LoadOpConversion, StoreOpConversion,
             FieldAddrOpConversion, BorrowOpConversion, RawAddressOpConversion>(
            typeConverter, context);
    populateFunctionOpInterfaceTypeConversionPattern<func::FuncOp>(
        patterns, typeConverter);
    populateCallOpTypeConversionPattern(patterns, typeConverter);
    populateReturnOpTypeConversionPattern(patterns, typeConverter);

    if (failed(applyPartialConversion(module, target, std::move(patterns))))
      signalPassFailure();
  }
};
} // namespace

std::unique_ptr<Pass> mlir::createConvertRustTypedMemoryToLLVMPass() {
  return std::make_unique<ConvertRustTypedMemoryToLLVMPass>();
}
