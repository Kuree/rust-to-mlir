//===- OpCreateCompat.h - MLIR op creation compatibility -------*- C++ -*-===//
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef RUST_TO_LLVM_SUPPORT_OPCREATECOMPAT_H
#define RUST_TO_LLVM_SUPPORT_OPCREATECOMPAT_H

#include "mlir/IR/Builders.h"
#include "mlir/IR/Location.h"

#include <type_traits>
#include <utility>

namespace mlir::rust {
namespace detail {
template <typename OpT, typename BuilderT, typename... Args>
using OpCreateExpr =
    decltype(OpT::create(std::declval<BuilderT &>(), std::declval<Location>(),
                         std::declval<Args>()...));

template <typename OpT, typename BuilderT, typename Enable, typename... Args>
struct HasBuilderCreateOp : std::false_type {};

template <typename OpT, typename BuilderT, typename... Args>
struct HasBuilderCreateOp<
    OpT, BuilderT, std::void_t<OpCreateExpr<OpT, BuilderT, Args...>>, Args...>
    : std::true_type {};
} // namespace detail

template <typename OpT, typename BuilderT, typename... Args>
OpT createOp(BuilderT &builder, Location loc, Args &&...args) {
  if constexpr (detail::HasBuilderCreateOp<OpT, BuilderT, void,
                                           Args &&...>::value) {
    return OpT::create(builder, loc, std::forward<Args>(args)...);
  } else {
    return builder.template create<OpT>(loc, std::forward<Args>(args)...);
  }
}
} // namespace mlir::rust

#endif // RUST_TO_LLVM_SUPPORT_OPCREATECOMPAT_H
