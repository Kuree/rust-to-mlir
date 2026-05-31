//===- RustTypedToFunc.cpp - Rust typed to func conversion -----*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Conversion/RustTypedToFunc/RustTypedToFunc.h"

#include "RustToMLIR/Support/OpCreateCompat.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustMIR/IR/RustTypes.h"
#include "mlir/Dialect/RustTyped/IR/RustTypedOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"

namespace mlir {
#define GEN_PASS_DEF_CONVERTRUSTTYPEDTOFUNCPASS
#include "mlir/Conversion/RustTypedToFunc/RustTypedToFuncPasses.h.inc"
} // namespace mlir

using namespace mlir;

namespace {
SmallVector<Type> getTopLevelReturnTypes(rust::mir::TypedFuncOp typedFunc) {
  SmallVector<Type> resultTypes;
  Region &body = typedFunc.getBody();
  if (body.empty())
    return resultTypes;

  for (Operation &op : body.front()) {
    if (auto ret = dyn_cast<rust::mir::TypedReturnOp>(op)) {
      for (Value operand : ret.getValues())
        resultTypes.push_back(operand.getType());
      break;
    }
  }
  return resultTypes;
}

bool isArgumentSlot(rust::mir::LocalSlotOp op) {
  if (std::optional<rust::mir::RustLocalRole> role = op.getRole())
    return *role == rust::mir::RustLocalRole::Arg;
  return false;
}

bool isUnusedUnitSlot(rust::mir::LocalSlotOp op) {
  return op.getSlot().use_empty() &&
         isa<rust::mir::UnitType>(op.getSlot().getType().getElementType());
}

SmallVector<rust::mir::LocalSlotOp>
collectArgumentSlots(rust::mir::TypedFuncOp typedFunc) {
  SmallVector<rust::mir::LocalSlotOp> argSlots;
  Region &body = typedFunc.getBody();
  if (body.empty())
    return argSlots;

  for (Operation &op : body.front()) {
    auto slot = dyn_cast<rust::mir::LocalSlotOp>(op);
    if (slot && isArgumentSlot(slot))
      argSlots.push_back(slot);
  }

  llvm::sort(argSlots,
             [](rust::mir::LocalSlotOp lhs, rust::mir::LocalSlotOp rhs) {
               return lhs.getIndex() < rhs.getIndex();
             });
  return argSlots;
}

SmallVector<Type> getArgumentTypes(ArrayRef<rust::mir::LocalSlotOp> argSlots) {
  SmallVector<Type> argTypes;
  argTypes.reserve(argSlots.size());
  for (rust::mir::LocalSlotOp slot : argSlots)
    argTypes.push_back(slot.getSlot().getType().getElementType());
  return argTypes;
}

void copyNonSymbolAttrs(Operation *from, Operation *to) {
  for (NamedAttribute attr : from->getAttrs()) {
    StringRef name = attr.getName().getValue();
    if (name == SymbolTable::getSymbolAttrName())
      continue;
    if (name == SymbolTable::getVisibilityAttrName() || name.contains(".")) {
      to->setAttr(attr.getName(), attr.getValue());
      continue;
    }

    std::string rustName = ("rust." + name).str();
    to->setAttr(StringAttr::get(to->getContext(), rustName), attr.getValue());
  }
}

struct TypedFuncOpConversion
    : public OpConversionPattern<rust::mir::TypedFuncOp> {
  using OpConversionPattern<rust::mir::TypedFuncOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rust::mir::TypedFuncOp typedFunc, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const final {
    SmallVector<rust::mir::LocalSlotOp> argSlots =
        collectArgumentSlots(typedFunc);
    SmallVector<Type> argTypes = getArgumentTypes(argSlots);
    SmallVector<Type> resultTypes = getTopLevelReturnTypes(typedFunc);
    FunctionType functionType = rewriter.getFunctionType(argTypes, resultTypes);

    auto func = mlir::rust::createOp<func::FuncOp>(
        rewriter, typedFunc.getLoc(), typedFunc.getSymName(), functionType);
    copyNonSymbolAttrs(typedFunc.getOperation(), func.getOperation());

    Block *entry = func.addEntryBlock();
    llvm::DenseMap<int64_t, BlockArgument> blockArgsByLocalIndex;
    for (auto [position, slot] : llvm::enumerate(argSlots))
      blockArgsByLocalIndex[static_cast<int64_t>(slot.getIndex())] =
          entry->getArgument(position);

    IRMapping mapping;
    rewriter.setInsertionPointToEnd(entry);

    Region &typedBody = typedFunc.getBody();
    if (!typedBody.empty()) {
      for (Operation &op : typedBody.front()) {
        if (auto ret = dyn_cast<rust::mir::TypedReturnOp>(op)) {
          SmallVector<Value> operands;
          operands.reserve(ret.getNumOperands());
          for (Value operand : ret.getValues())
            operands.push_back(mapping.lookupOrDefault(operand));
          mlir::rust::createOp<func::ReturnOp>(rewriter, ret.getLoc(),
                                               operands);
          continue;
        }
        if (auto slot = dyn_cast<rust::mir::LocalSlotOp>(op))
          if (!isArgumentSlot(slot) && isUnusedUnitSlot(slot))
            continue;
        Operation *cloned = rewriter.clone(op, mapping);
        auto clonedSlot = dyn_cast<rust::mir::LocalSlotOp>(cloned);
        if (!clonedSlot || !isArgumentSlot(clonedSlot))
          continue;

        auto blockArgIt = blockArgsByLocalIndex.find(
            static_cast<int64_t>(clonedSlot.getIndex()));
        if (blockArgIt == blockArgsByLocalIndex.end())
          return clonedSlot.emitError(
              "missing function argument for local slot");
        mlir::rust::createOp<rust::mir::StoreOp>(rewriter, clonedSlot.getLoc(),
                                                 blockArgIt->second,
                                                 clonedSlot.getSlot());
      }
    }

    if (entry->empty() || !entry->back().hasTrait<OpTrait::IsTerminator>())
      mlir::rust::createOp<func::ReturnOp>(rewriter, typedFunc.getLoc());

    rewriter.eraseOp(typedFunc);
    return success();
  }
};

struct ConvertRustTypedToFuncPass
    : public mlir::impl::ConvertRustTypedToFuncPassBase<
          ConvertRustTypedToFuncPass> {
  void runOnOperation() final {
    ModuleOp module = getOperation();
    MLIRContext *context = module.getContext();
    ConversionTarget target(*context);
    target.addLegalDialect<func::FuncDialect, rust::mir::RustMIRDialect>();
    target.addIllegalOp<rust::mir::TypedFuncOp>();
    target.markUnknownOpDynamicallyLegal([](Operation *) { return true; });

    RewritePatternSet patterns(context);
    patterns.add<TypedFuncOpConversion>(context);
    if (failed(applyPartialConversion(module, target, std::move(patterns))))
      signalPassFailure();
  }
};
} // namespace

std::unique_ptr<Pass> mlir::createConvertRustTypedToFuncPass() {
  return std::make_unique<ConvertRustTypedToFuncPass>();
}
