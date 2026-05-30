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
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/STLExtras.h"

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
