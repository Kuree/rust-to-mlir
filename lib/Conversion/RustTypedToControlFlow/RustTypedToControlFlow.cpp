//===- RustTypedToControlFlow.cpp - Rust typed to cf conversion -*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Conversion/RustTypedToControlFlow/RustTypedToControlFlow.h"

#include "RustToLLVM/Support/OpCreateCompat.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/RustMIR/IR/RustMIRDialect.h"
#include "mlir/Dialect/RustTyped/IR/RustTypedOps.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <limits>

namespace mlir {
#define GEN_PASS_DEF_CONVERTRUSTTYPEDTOCONTROLFLOWPASS
#include "mlir/Conversion/RustTypedToControlFlow/RustTypedToControlFlowPasses.h.inc"
} // namespace mlir

using namespace mlir;

namespace {
std::optional<int64_t> getI64Attr(DictionaryAttr dict, StringRef name) {
  if (!dict)
    return std::nullopt;
  auto attr = dyn_cast_or_null<IntegerAttr>(dict.get(name));
  if (!attr)
    return std::nullopt;
  return attr.getInt();
}

bool isDefaultTargetName(StringRef name) {
  return name == "default" || name == "otherwise";
}

std::optional<int32_t> parseSwitchCaseName(StringRef name) {
  if (!name.consume_front("case_"))
    name.consume_front("case");

  int64_t value = 0;
  if (name.getAsInteger(10, value))
    return std::nullopt;
  if (value < std::numeric_limits<int32_t>::min() ||
      value > std::numeric_limits<int32_t>::max())
    return std::nullopt;
  return static_cast<int32_t>(value);
}

SmallVector<rust::mir::TypedBlockOp>
collectTopLevelTypedBlocks(func::FuncOp func) {
  SmallVector<rust::mir::TypedBlockOp> typedBlocks;
  Region &body = func.getBody();
  if (body.empty())
    return typedBlocks;

  for (Operation &op : body.front())
    if (auto typedBlock = dyn_cast<rust::mir::TypedBlockOp>(op))
      typedBlocks.push_back(typedBlock);
  return typedBlocks;
}

LogicalResult
inferNestedReturnTypes(ArrayRef<rust::mir::TypedBlockOp> typedBlocks,
                       SmallVectorImpl<Type> &resultTypes) {
  bool seenReturn = false;
  for (rust::mir::TypedBlockOp typedBlock : typedBlocks) {
    Region &region = typedBlock.getBody();
    if (region.empty())
      continue;
    for (Operation &op : region.front()) {
      if (auto ret = dyn_cast<rust::mir::TypedReturnOp>(op)) {
        SmallVector<Type> currentTypes;
        for (Value operand : ret.getValues())
          currentTypes.push_back(operand.getType());

        if (!seenReturn) {
          resultTypes.assign(currentTypes.begin(), currentTypes.end());
          seenReturn = true;
          continue;
        }

        if (currentTypes.size() != resultTypes.size() ||
            !llvm::equal(currentTypes, resultTypes))
          return ret.emitError("expected all rust.typed.return ops to have "
                               "matching operand types");
      }
    }
  }
  return success();
}

Operation *findTypedTerminator(Block &block) {
  for (Operation &op : block) {
    if (isa<rust::mir::TypedGotoOp, rust::mir::TypedReturnOp,
            rust::mir::TypedSwitchIntOp>(op))
      return &op;
  }
  return nullptr;
}

LogicalResult
lowerTypedTerminator(Operation *op,
                     const llvm::DenseMap<int64_t, Block *> &blocks,
                     ConversionPatternRewriter &rewriter) {
  if (op != &op->getBlock()->back())
    return op->emitError("expected rust.typed terminator to end its block");

  rewriter.setInsertionPoint(op);
  if (auto ret = dyn_cast<rust::mir::TypedReturnOp>(op)) {
    rewriter.replaceOpWithNewOp<func::ReturnOp>(ret, ret.getValues());
    return success();
  }

  if (auto gotoOp = dyn_cast<rust::mir::TypedGotoOp>(op)) {
    int64_t target = static_cast<int64_t>(gotoOp.getTarget());
    auto blockIt = blocks.find(target);
    if (blockIt == blocks.end())
      return op->emitError(
          "expected rust.typed.goto to reference a known block");
    rewriter.replaceOpWithNewOp<cf::BranchOp>(gotoOp, blockIt->second);
    return success();
  }

  if (auto switchOp = dyn_cast<rust::mir::TypedSwitchIntOp>(op)) {
    auto targets = switchOp.getTargetsAttr();
    std::optional<int64_t> trueTarget = getI64Attr(targets, "true");
    std::optional<int64_t> falseTarget = getI64Attr(targets, "false");
    if (trueTarget || falseTarget) {
      if (!trueTarget || !falseTarget || !blocks.count(*trueTarget) ||
          !blocks.count(*falseTarget))
        return op->emitError("expected rust.typed.switch_int targets with true "
                             "and false blocks");
      if (!switchOp.getDiscr().getType().isSignlessInteger(1))
        return op->emitError(
            "expected boolean rust.typed.switch_int discriminator to be i1");

      rewriter.replaceOpWithNewOp<cf::CondBranchOp>(
          switchOp, switchOp.getDiscr(), blocks.lookup(*trueTarget),
          ValueRange(), blocks.lookup(*falseTarget), ValueRange());
      return success();
    }

    std::optional<int64_t> defaultTarget = getI64Attr(targets, "default");
    if (!defaultTarget)
      defaultTarget = getI64Attr(targets, "otherwise");
    if (!defaultTarget || !blocks.count(*defaultTarget))
      return op->emitError(
          "expected rust.typed.switch_int default target block");

    SmallVector<std::pair<int32_t, Block *>> cases;
    for (NamedAttribute attr : targets) {
      StringRef name = attr.getName().getValue();
      if (isDefaultTargetName(name))
        continue;

      std::optional<int32_t> value = parseSwitchCaseName(name);
      auto target = dyn_cast<IntegerAttr>(attr.getValue());
      if (!value || !target)
        return op->emitError(
            "expected rust.typed.switch_int integer case target");

      int64_t targetIndex = target.getInt();
      auto blockIt = blocks.find(targetIndex);
      if (blockIt == blocks.end())
        return op->emitError(
            "expected rust.typed.switch_int case to reference a known block");
      cases.push_back({*value, blockIt->second});
    }

    llvm::sort(cases, [](const auto &lhs, const auto &rhs) {
      return lhs.first < rhs.first;
    });

    if (cases.empty()) {
      rewriter.replaceOpWithNewOp<cf::BranchOp>(switchOp,
                                                blocks.lookup(*defaultTarget));
      return success();
    }

    SmallVector<int32_t> caseValues;
    SmallVector<Block *> caseDestinations;
    SmallVector<ValueRange> caseOperands;
    caseValues.reserve(cases.size());
    caseDestinations.reserve(cases.size());
    caseOperands.resize(cases.size());
    for (auto [value, block] : cases) {
      caseValues.push_back(value);
      caseDestinations.push_back(block);
    }

    mlir::rust::createOp<cf::SwitchOp>(
        rewriter, switchOp.getLoc(), switchOp.getDiscr(),
        blocks.lookup(*defaultTarget), ValueRange(), caseValues,
        caseDestinations, caseOperands);
    rewriter.eraseOp(switchOp);
    return success();
  }

  return failure();
}

struct TypedBlockConversion
    : public OpConversionPattern<rust::mir::TypedBlockOp> {
  using OpConversionPattern<rust::mir::TypedBlockOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rust::mir::TypedBlockOp rootBlock, OpAdaptor,
                  ConversionPatternRewriter &rewriter) const final {
    auto func = rootBlock->getParentOfType<func::FuncOp>();
    if (!func || rootBlock->getBlock() != &func.getBody().front())
      return failure();

    SmallVector<rust::mir::TypedBlockOp> typedBlocks =
        collectTopLevelTypedBlocks(func);
    if (typedBlocks.empty())
      return failure();

    SmallVector<Type> resultTypes;
    if (failed(inferNestedReturnTypes(typedBlocks, resultTypes)))
      return failure();
    Region &body = func.getBody();
    Block &entry = body.front();
    int64_t firstIndex = static_cast<int64_t>(typedBlocks.front().getIndex());

    llvm::DenseMap<int64_t, Block *> blockMap;
    for (rust::mir::TypedBlockOp typedBlock : typedBlocks) {
      int64_t index = static_cast<int64_t>(typedBlock.getIndex());
      if (blockMap.contains(index))
        return typedBlock.emitError("duplicate rust.typed.block index");
      blockMap[index] = rewriter.createBlock(&body, body.end());
    }

    for (auto [position, typedBlock] : llvm::enumerate(typedBlocks)) {
      int64_t index = static_cast<int64_t>(typedBlock.getIndex());
      Block *targetBlock = blockMap.lookup(index);
      Region &region = typedBlock.getBody();

      if (!region.empty())
        rewriter.inlineBlockBefore(&region.front(), targetBlock,
                                   targetBlock->end());

      if (Operation *terminator = findTypedTerminator(*targetBlock)) {
        if (failed(lowerTypedTerminator(terminator, blockMap, rewriter)))
          return failure();
        continue;
      }

      rewriter.setInsertionPointToEnd(targetBlock);
      if (position + 1 < typedBlocks.size()) {
        int64_t nextIndex =
            static_cast<int64_t>(typedBlocks[position + 1].getIndex());
        mlir::rust::createOp<cf::BranchOp>(rewriter, typedBlock.getLoc(),
                                           blockMap.lookup(nextIndex));
      } else {
        mlir::rust::createOp<func::ReturnOp>(rewriter, typedBlock.getLoc());
      }
    }

    for (rust::mir::TypedBlockOp typedBlock : typedBlocks)
      rewriter.eraseOp(typedBlock);

    if (!entry.empty() && entry.back().hasTrait<OpTrait::IsTerminator>())
      rewriter.eraseOp(&entry.back());

    rewriter.setInsertionPointToEnd(&entry);
    mlir::rust::createOp<cf::BranchOp>(rewriter, func.getLoc(),
                                       blockMap.lookup(firstIndex));

    rewriter.modifyOpInPlace(func, [&] {
      func.setFunctionType(FunctionType::get(
          func.getContext(), func.getArgumentTypes(), resultTypes));
    });
    return success();
  }
};

struct TypedAssertConversion
    : public OpConversionPattern<rust::mir::TypedAssertOp> {
  using OpConversionPattern<rust::mir::TypedAssertOp>::OpConversionPattern;

  LogicalResult
  matchAndRewrite(rust::mir::TypedAssertOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    StringRef msg = "rust assert";
    if (auto msgAttr = op.getMsgAttr())
      msg = msgAttr.getValue();
    rewriter.replaceOpWithNewOp<cf::AssertOp>(op, adaptor.getCond(), msg);
    return success();
  }
};

struct ConvertRustTypedToControlFlowPass
    : public mlir::impl::ConvertRustTypedToControlFlowPassBase<
          ConvertRustTypedToControlFlowPass> {
  void runOnOperation() final {
    ModuleOp module = getOperation();
    MLIRContext *context = module.getContext();
    ConversionTarget target(*context);
    target.addLegalDialect<cf::ControlFlowDialect, func::FuncDialect,
                           rust::mir::RustMIRDialect>();
    target
        .addIllegalOp<rust::mir::TypedBlockOp, rust::mir::TypedGotoOp,
                      rust::mir::TypedReturnOp, rust::mir::TypedSwitchIntOp,
                      rust::mir::TypedAssertOp>();
    target.markUnknownOpDynamicallyLegal([](Operation *) { return true; });

    RewritePatternSet patterns(context);
    patterns.add<TypedBlockConversion, TypedAssertConversion>(context);
    if (failed(applyPartialConversion(module, target, std::move(patterns))))
      signalPassFailure();
  }
};
} // namespace

std::unique_ptr<Pass> mlir::createConvertRustTypedToControlFlowPass() {
  return std::make_unique<ConvertRustTypedToControlFlowPass>();
}
