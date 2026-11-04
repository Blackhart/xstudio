// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/ir.hpp
 * @brief Intermediate representation of a query, independent of any backend.
 */
#pragma once

#include "pms/value.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pms {

/**
 * @brief Verb carried by an intermediate representation once it is executed.
 */
enum class Op {
    Find, ///< Read matching rows. The only verb in this slice.
};

/**
 * @brief Operator stored on an expression node.
 */
enum class ExprOp {
    Eq,  ///< Field equals the leaf value.
    Ne,  ///< Field differs from the leaf value.
    And, ///< Group that requires every child.
    Or,  ///< Group that requires one child.
    Not, ///< Group with exactly one child.
};

/**
 * @brief Role of an expression node.
 */
enum class ExprKind {
    Field, ///< Comparison of one field of the queried entity.
    Scope, ///< Comparison of one parent name from In().
    Group, ///< `and`, `or`, or `not` applied to child nodes.
};

/**
 * @brief One immutable node of an In or Where tree.
 *
 * A field node has `key`, `op` (`eq` or `ne`), and `value`. A scope node has
 * the parent entity name as `key` and either the parent `id` (int) or `name`
 * (string) as `value`. A group node has `op` (`and`, `or`, or `not`) and
 * `args`. Nodes are shared and never mutated after the builder returns them.
 */
struct ExprNode {
    ExprOp op = ExprOp::Eq;    ///< Comparison or group operator.
    ExprKind kind = ExprKind::Field; ///< Field leaf, scope leaf, or group.
    std::string key;           ///< Field name or parent entity. Empty on a group.
    std::optional<Value> value; ///< Leaf value. Empty on a group.
    std::vector<std::shared_ptr<const ExprNode>> args; ///< Children of a group.
};

/**
 * @brief Query handed to validate() and to the backend.
 *
 * Query::ir() leaves `op` empty and does not validate. find() sets `op` to
 * Find before validation. `in` and `where` are null when the clause is absent.
 */
struct Ir {
    std::optional<Op> op; ///< Empty until a verb runs.
    std::string entity;   ///< Domain entity name.
    std::shared_ptr<const ExprNode> in;    ///< Parent scope tree, or null.
    std::shared_ptr<const ExprNode> where; ///< Field filter tree, or null.
    std::optional<std::size_t> limit;      ///< Page size when set. Empty means every match.
    std::optional<std::string> cursor;     ///< Keyset cursor from a prior Page::next_cursor.
};

/**
 * @brief Renders an intermediate representation as stable text.
 *
 * Keys inside a leaf use the YAML names. Strings are quoted, dates are
 * `YYYY-MM-DD`, and group children are indented by two spaces. The text ends
 * with a newline and is what the tests compare.
 *
 * @param ir Tree to render. `op` may be empty.
 * @return The multi-line dump.
 * @throw Error When a field leaf has no value or a group child is null.
 */
std::string dump(const Ir &ir);

} // namespace pms
