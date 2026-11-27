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
    Find,  ///< Read matching rows.
    Count, ///< Return how many rows match. No pagination.
};

/**
 * @brief Operator stored on an expression node.
 */
enum class ExprOp {
    Eq,              ///< Field equals the leaf value.
    Ne,              ///< Field differs from the leaf value.
    Contains,        ///< String field contains the leaf substring (case-insensitive).
    NotContains,     ///< String field does not contain the leaf.
    StartsWith,      ///< String field starts with the leaf.
    EndsWith,        ///< String field ends with the leaf.
    In,              ///< Field is one of the leaf values.
    NotIn,           ///< Field is none of the leaf values.
    Gt,              ///< Field is greater than the leaf.
    Lt,              ///< Field is less than the leaf.
    Between,         ///< Field lies in the inclusive [low, high] bounds.
    NotBetween,      ///< Field lies outside the inclusive [low, high] bounds.
    InLast,          ///< Datetime is inside a past period.
    NotInLast,       ///< Datetime is outside a past period.
    InNext,          ///< Datetime is inside a coming period.
    NotInNext,       ///< Datetime is outside a coming period.
    InCalendarDay,   ///< Datetime falls on a day offset from today.
    InCalendarWeek,  ///< Datetime falls in a week offset from this week.
    InCalendarMonth, ///< Datetime falls in a month offset from this month.
    InCalendarYear,  ///< Datetime falls in a year offset from this year.
    NameContains,    ///< Linked entity name contains the leaf.
    NameNotContains, ///< Linked entity name does not contain the leaf.
    NameIs,          ///< Linked entity name equals the leaf.
    TypeIs,          ///< Linked entity type equals the leaf.
    TypeIsNot,       ///< Linked entity type differs from the leaf.
    And,             ///< Group that requires every child.
    Or,              ///< Group that requires one child.
    Not,             ///< Group with exactly one child.
};

/**
 * @brief Role of an expression node.
 */
enum class ExprKind {
    Field, ///< Field of the queried entity (Where) or of a parent (In).
    Group, ///< `and`, `or`, or `not` applied to child nodes.
};

/**
 * @brief One immutable node of an In or Where tree.
 *
 * A field node has `key`, a comparison `op`, and `value`. For
 * In() leaves, `parent` holds the domain parent name (`shot`, `playlist`, …).
 * Where() leaves leave `parent` empty. A group node has `op` (`and`, `or`, or
 * `not`) and `args`. Nodes are shared and never mutated after the builder
 * returns them.
 */
struct ExprNode {
    ExprOp op = ExprOp::Eq;              ///< Comparison or group operator.
    ExprKind kind = ExprKind::Field;     ///< Field leaf or group.
    std::string key;                     ///< Domain field name. Empty on a group.
    std::string parent;                  ///< Domain parent for In leaves. Empty on Where.
    std::optional<Value> value;          ///< Leaf value. Empty on a group.
    std::vector<std::shared_ptr<const ExprNode>> args; ///< Children of a group.
};

/**
 * @brief Query handed to validate() and to the backend.
 *
 * Query::ir() leaves `op` empty and does not validate. find() and count() set
 * `op` before validation. `in` and `where` are null when the clause is absent.
 */
struct Ir {
    std::optional<Op> op; ///< Empty until a verb runs.
    std::string entity;   ///< Domain entity name.
    std::shared_ptr<const ExprNode> in;    ///< Parent field filter tree, or null.
    std::shared_ptr<const ExprNode> where; ///< Field filter tree, or null.
    std::optional<std::size_t> limit;      ///< Page size when set. Empty means every match.
    std::optional<std::string> cursor;     ///< Keyset cursor from a prior Page::next_cursor.
    /**
     * @brief Domain field keys to load on find. Empty means every mapped field.
     *
     * Validation ensures each key exists on the entity. Query::Fields always
     * includes `id` and `name` so checkRecord still passes.
     */
    std::optional<std::vector<std::string>> fields;
};

/**
 * @brief Renders an intermediate representation as stable text.
 *
 * Keys inside a leaf use the YAML names. In leaves print as
 * `field <parent>.<key>`. Strings are quoted, dates are `YYYY-MM-DD`, and group
 * children are indented by two spaces. The text ends with a newline and is what
 * the tests compare.
 *
 * @param ir Tree to render. `op` may be empty.
 * @return The multi-line dump.
 * @throw Error When a field leaf has no value or a group child is null.
 */
std::string dump(const Ir &ir);

} // namespace pms
