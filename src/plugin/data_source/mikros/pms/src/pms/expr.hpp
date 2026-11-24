// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/expr.hpp
 * @brief Field proxies and the operators that build Where and In trees.
 */
#pragma once

#include "pms/ir.hpp"

#include <type_traits>
#include <utility>

namespace pms {

/**
 * @brief Stops a function parameter from taking part in template deduction.
 *
 * Comparison operators deduce the value type from the field proxy, then let
 * the argument convert to it. `name == "paw01"` therefore builds a string leaf.
 *
 * @tparam T Value type of the field.
 */
template <typename T> struct NonDeduced {
    using type = T; ///< The type, hidden from deduction.
};

/**
 * @brief Compile-time name of one domain field.
 *
 * The proxy is not the value. Comparing it builds a FieldExpr and leaves the
 * proxy unchanged. `==`, `!=`, `contains()`, `notContains()`, `startsWith()`,
 * `endsWith()`, `isIn()`, `isNotIn()`, `greaterThan()`, `lessThan()`,
 * `between()`, `inLast()`, `inCalendarDay()` / `inCalendarWeek()` /
 * `inCalendarMonth()`, and `nameContains()` / `nameStartsWith()` / `nameIs()`
 * are the field operators.
 *
 * @tparam Entity Entity tag that owns the field.
 * @tparam T C++ type of the field value.
 */
template <typename Entity, typename T> struct Field {
    const char *key = nullptr; ///< YAML field name, such as `"name"`.

    /**
     * @brief Binds the proxy to a YAML key.
     * @param fieldKey YAML field name. Must outlive the proxy.
     */
    constexpr explicit Field(const char *fieldKey) : key(fieldKey) {}
};

/**
 * @brief Field-tree whose leaves all belong to one entity.
 *
 * Used for Where() on the queried entity and for In() on one parent. `&&`,
 * `||`, and `!` combine expressions of the same entity. They build both sides:
 * the overloaded operators do not short-circuit. C++ precedence applies, so
 * parentheses are significant.
 *
 * @tparam Entity Entity tag the leaves belong to.
 */
template <typename Entity> struct FieldExpr {
    std::shared_ptr<const ExprNode> node; ///< Root of this expression.
};

/**
 * @brief Builds a field comparison leaf.
 * @tparam Entity Entity tag that owns the field.
 * @tparam T C++ type of the field value.
 * @param field Proxy that supplies the YAML key.
 * @param op Comparison operator stored on the leaf.
 * @param value Scalar stored on the leaf.
 * @return Expression holding the new node.
 */
template <typename Entity, typename T>
FieldExpr<Entity> makeComparison(const Field<Entity, T> &field, ExprOp op, Value value) {
    auto node = std::make_shared<ExprNode>();
    node->op = op;
    node->kind = ExprKind::Field;
    node->key = field.key;
    node->value = std::move(value);
    return FieldExpr<Entity>{std::move(node)};
}

/**
 * @brief Compares a field for equality.
 * @tparam Entity Entity tag that owns the field.
 * @tparam T C++ type of the field value.
 * @param field Proxy on the left.
 * @param value Value converted to `T`.
 * @return A field leaf with `op` Eq.
 */
template <typename Entity, typename T>
FieldExpr<Entity> operator==(
    const Field<Entity, T> &field, const typename NonDeduced<T>::type &value) {
    return makeComparison(field, ExprOp::Eq, Value{value});
}

/**
 * @brief Compares a field for inequality.
 * @tparam Entity Entity tag that owns the field.
 * @tparam T C++ type of the field value.
 * @param field Proxy on the left.
 * @param value Value converted to `T`.
 * @return A field leaf with `op` Ne.
 */
template <typename Entity, typename T>
FieldExpr<Entity> operator!=(
    const Field<Entity, T> &field, const typename NonDeduced<T>::type &value) {
    return makeComparison(field, ExprOp::Ne, Value{value});
}

/**
 * @brief Tests whether a string field contains a substring.
 * @tparam Entity Entity tag that owns the field.
 * @param field String proxy on the left.
 * @param text Substring to look for (case-insensitive on the backends).
 * @return A field leaf with `op` Contains.
 */
template <typename Entity>
FieldExpr<Entity> contains(const Field<Entity, std::string> &field, const std::string &text) {
    return makeComparison(field, ExprOp::Contains, Value{text});
}

/**
 * @brief Tests whether a string field does not contain a substring.
 * @tparam Entity Entity tag that owns the field.
 * @param field String proxy on the left.
 * @param text Substring that must be absent.
 * @return A field leaf with `op` NotContains.
 */
template <typename Entity>
FieldExpr<Entity> notContains(const Field<Entity, std::string> &field, const std::string &text) {
    return makeComparison(field, ExprOp::NotContains, Value{text});
}

/**
 * @brief Compares a date field with an ISO string.
 * @tparam Entity Entity tag that owns the field.
 * @param field Date proxy on the left.
 * @param text Date in the form `YYYY-MM-DD`.
 * @return A date leaf with `op` Eq.
 * @throw Error When `text` is not a real calendar day.
 */
template <typename Entity>
FieldExpr<Entity> operator==(const Field<Entity, Date> &field, const std::string &text) {
    return makeComparison(field, ExprOp::Eq, Value{parseDate(text)});
}

/**
 * @brief Compares a date field with an ISO string for inequality.
 * @tparam Entity Entity tag that owns the field.
 * @param field Date proxy on the left.
 * @param text Date in the form `YYYY-MM-DD`.
 * @return A date leaf with `op` Ne.
 * @throw Error When `text` is not a real calendar day.
 */
template <typename Entity>
FieldExpr<Entity> operator!=(const Field<Entity, Date> &field, const std::string &text) {
    return makeComparison(field, ExprOp::Ne, Value{parseDate(text)});
}

/**
 * @brief Tests whether a string field starts with a prefix.
 * @tparam Entity Entity tag that owns the field.
 * @param field String proxy on the left.
 * @param text Prefix to look for.
 * @return A field leaf with `op` StartsWith.
 */
template <typename Entity>
FieldExpr<Entity> startsWith(const Field<Entity, std::string> &field, const std::string &text) {
    return makeComparison(field, ExprOp::StartsWith, Value{text});
}

/**
 * @brief Tests whether a string field ends with a suffix.
 * @tparam Entity Entity tag that owns the field.
 * @param field String proxy on the left.
 * @param text Suffix to look for.
 * @return A field leaf with `op` EndsWith.
 */
template <typename Entity>
FieldExpr<Entity> endsWith(const Field<Entity, std::string> &field, const std::string &text) {
    return makeComparison(field, ExprOp::EndsWith, Value{text});
}

/**
 * @brief Tests whether a string field is one of the given values.
 * @tparam Entity Entity tag that owns the field.
 * @param field String proxy on the left.
 * @param values Accepted values. Empty is rejected at validation.
 * @return A field leaf with `op` In.
 */
template <typename Entity>
FieldExpr<Entity> isIn(
    const Field<Entity, std::string> &field, std::vector<std::string> values) {
    return makeComparison(field, ExprOp::In, Value{std::move(values)});
}

/**
 * @brief Tests whether an int field is one of the given values.
 * @tparam Entity Entity tag that owns the field.
 * @param field Int proxy on the left.
 * @param values Accepted values. Empty is rejected at validation.
 * @return A field leaf with `op` In.
 */
template <typename Entity>
FieldExpr<Entity> isIn(
    const Field<Entity, std::int64_t> &field, std::vector<std::int64_t> values) {
    return makeComparison(field, ExprOp::In, Value{std::move(values)});
}

/**
 * @brief Tests whether a string field is none of the given values.
 * @tparam Entity Entity tag that owns the field.
 * @param field String proxy on the left.
 * @param values Rejected values. Empty is rejected at validation.
 * @return A field leaf with `op` NotIn.
 */
template <typename Entity>
FieldExpr<Entity> isNotIn(
    const Field<Entity, std::string> &field, std::vector<std::string> values) {
    return makeComparison(field, ExprOp::NotIn, Value{std::move(values)});
}

/**
 * @brief Tests whether an int field is none of the given values.
 * @tparam Entity Entity tag that owns the field.
 * @param field Int proxy on the left.
 * @param values Rejected values. Empty is rejected at validation.
 * @return A field leaf with `op` NotIn.
 */
template <typename Entity>
FieldExpr<Entity> isNotIn(
    const Field<Entity, std::int64_t> &field, std::vector<std::int64_t> values) {
    return makeComparison(field, ExprOp::NotIn, Value{std::move(values)});
}

/**
 * @brief Tests whether an int field is greater than a number.
 * @tparam Entity Entity tag that owns the field.
 * @param field Int proxy on the left.
 * @param value Exclusive lower bound.
 * @return A field leaf with `op` Gt.
 */
template <typename Entity>
FieldExpr<Entity> greaterThan(const Field<Entity, std::int64_t> &field, std::int64_t value) {
    return makeComparison(field, ExprOp::Gt, Value{value});
}

/**
 * @brief Tests whether an int field is less than a number.
 * @tparam Entity Entity tag that owns the field.
 * @param field Int proxy on the left.
 * @param value Exclusive upper bound.
 * @return A field leaf with `op` Lt.
 */
template <typename Entity>
FieldExpr<Entity> lessThan(const Field<Entity, std::int64_t> &field, std::int64_t value) {
    return makeComparison(field, ExprOp::Lt, Value{value});
}

/**
 * @brief Tests whether a date field is after a calendar day.
 * @tparam Entity Entity tag that owns the field.
 * @param field Date proxy on the left.
 * @param text Day in the form `YYYY-MM-DD`.
 * @return A date leaf with `op` Gt.
 * @throw Error When `text` is not a real calendar day.
 */
template <typename Entity>
FieldExpr<Entity> greaterThan(const Field<Entity, Date> &field, const std::string &text) {
    return makeComparison(field, ExprOp::Gt, Value{parseDate(text)});
}

/**
 * @brief Tests whether a date field is before a calendar day.
 * @tparam Entity Entity tag that owns the field.
 * @param field Date proxy on the left.
 * @param text Day in the form `YYYY-MM-DD`.
 * @return A date leaf with `op` Lt.
 * @throw Error When `text` is not a real calendar day.
 */
template <typename Entity>
FieldExpr<Entity> lessThan(const Field<Entity, Date> &field, const std::string &text) {
    return makeComparison(field, ExprOp::Lt, Value{parseDate(text)});
}

/**
 * @brief Compares a datetime field with a calendar day.
 * @tparam Entity Entity tag that owns the field.
 * @param field Datetime proxy on the left.
 * @param text Day in the form `YYYY-MM-DD`.
 * @return A date leaf with `op` Eq.
 * @throw Error When `text` is not a real calendar day.
 */
template <typename Entity>
FieldExpr<Entity> operator==(const Field<Entity, DateTime> &field, const std::string &text) {
    return makeComparison(field, ExprOp::Eq, Value{parseDate(text)});
}

/**
 * @brief Compares a datetime field with a calendar day for inequality.
 * @tparam Entity Entity tag that owns the field.
 * @param field Datetime proxy on the left.
 * @param text Day in the form `YYYY-MM-DD`.
 * @return A date leaf with `op` Ne.
 * @throw Error When `text` is not a real calendar day.
 */
template <typename Entity>
FieldExpr<Entity> operator!=(const Field<Entity, DateTime> &field, const std::string &text) {
    return makeComparison(field, ExprOp::Ne, Value{parseDate(text)});
}

/**
 * @brief Tests whether a datetime field is after a calendar day.
 * @tparam Entity Entity tag that owns the field.
 * @param field Datetime proxy on the left.
 * @param text Day in the form `YYYY-MM-DD`.
 * @return A date leaf with `op` Gt.
 * @throw Error When `text` is not a real calendar day.
 */
template <typename Entity>
FieldExpr<Entity> greaterThan(const Field<Entity, DateTime> &field, const std::string &text) {
    return makeComparison(field, ExprOp::Gt, Value{parseDate(text)});
}

/**
 * @brief Tests whether a datetime field is before a calendar day.
 * @tparam Entity Entity tag that owns the field.
 * @param field Datetime proxy on the left.
 * @param text Day in the form `YYYY-MM-DD`.
 * @return A date leaf with `op` Lt.
 * @throw Error When `text` is not a real calendar day.
 */
template <typename Entity>
FieldExpr<Entity> lessThan(const Field<Entity, DateTime> &field, const std::string &text) {
    return makeComparison(field, ExprOp::Lt, Value{parseDate(text)});
}

/**
 * @brief Tests whether a datetime field falls in a past period.
 * @tparam Entity Entity tag that owns the field.
 * @param field Datetime proxy on the left.
 * @param count Positive number of units.
 * @param unit `HOUR`, `DAY`, `WEEK`, `MONTH`, or `YEAR`.
 * @return A period leaf with `op` InLast.
 */
template <typename Entity>
FieldExpr<Entity> inLast(
    const Field<Entity, DateTime> &field, std::int64_t count, const std::string &unit) {
    return makeComparison(field, ExprOp::InLast, Value{Period{count, unit}});
}

/**
 * @brief Tests whether a datetime field falls on a day offset from today.
 * @tparam Entity Entity tag that owns the field.
 * @param field Datetime proxy on the left.
 * @param offset `0` is today, `1` is tomorrow, `-1` is yesterday.
 * @return An int leaf with `op` InCalendarDay.
 */
template <typename Entity>
FieldExpr<Entity> inCalendarDay(const Field<Entity, DateTime> &field, std::int64_t offset) {
    return makeComparison(field, ExprOp::InCalendarDay, Value{offset});
}

/**
 * @brief Tests whether a datetime field falls in a week offset from this week.
 * @tparam Entity Entity tag that owns the field.
 * @param field Datetime proxy on the left.
 * @param offset `0` is this week, `1` is next week, `-1` is last week.
 * @return An int leaf with `op` InCalendarWeek.
 */
template <typename Entity>
FieldExpr<Entity> inCalendarWeek(const Field<Entity, DateTime> &field, std::int64_t offset) {
    return makeComparison(field, ExprOp::InCalendarWeek, Value{offset});
}

/**
 * @brief Tests whether a datetime field falls in a month offset from this month.
 * @tparam Entity Entity tag that owns the field.
 * @param field Datetime proxy on the left.
 * @param offset `0` is this month, `1` is next month, `-1` is last month.
 * @return An int leaf with `op` InCalendarMonth.
 */
template <typename Entity>
FieldExpr<Entity> inCalendarMonth(const Field<Entity, DateTime> &field, std::int64_t offset) {
    return makeComparison(field, ExprOp::InCalendarMonth, Value{offset});
}

/**
 * @brief Tests whether an int field lies in an inclusive range.
 * @tparam Entity Entity tag that owns the field.
 * @param field Int proxy on the left.
 * @param low Inclusive lower bound.
 * @param high Inclusive upper bound.
 * @return A field leaf with `op` Between.
 */
template <typename Entity>
FieldExpr<Entity> between(
    const Field<Entity, std::int64_t> &field, std::int64_t low, std::int64_t high) {
    return makeComparison(field, ExprOp::Between, Value{IntBounds{low, high}});
}

/**
 * @brief Tests whether a date field lies in an inclusive day range.
 * @tparam Entity Entity tag that owns the field.
 * @param field Date proxy on the left.
 * @param low First day as `YYYY-MM-DD`.
 * @param high Last day as `YYYY-MM-DD`.
 * @return A field leaf with `op` Between.
 * @throw Error When a bound is not a real calendar day.
 */
template <typename Entity>
FieldExpr<Entity> between(
    const Field<Entity, Date> &field, const std::string &low, const std::string &high) {
    return makeComparison(field, ExprOp::Between, Value{DateBounds{parseDate(low), parseDate(high)}});
}

/**
 * @brief Tests whether a datetime field lies in an inclusive day range.
 * @tparam Entity Entity tag that owns the field.
 * @param field Datetime proxy on the left.
 * @param low First day as `YYYY-MM-DD`.
 * @param high Last day as `YYYY-MM-DD`.
 * @return A field leaf with `op` Between.
 * @throw Error When a bound is not a real calendar day.
 */
template <typename Entity>
FieldExpr<Entity> between(
    const Field<Entity, DateTime> &field, const std::string &low, const std::string &high) {
    return makeComparison(field, ExprOp::Between, Value{DateBounds{parseDate(low), parseDate(high)}});
}

/**
 * @brief Tests whether a linked entity name contains a substring.
 * @tparam Entity Entity tag that owns the field.
 * @param field Entity proxy on the left.
 * @param text Substring to look for in the linked name.
 * @return A field leaf with `op` NameContains.
 */
template <typename Entity>
FieldExpr<Entity> nameContains(const Field<Entity, EntityRef> &field, const std::string &text) {
    return makeComparison(field, ExprOp::NameContains, Value{text});
}

/**
 * @brief Tests whether a linked entity name starts with a prefix.
 * @tparam Entity Entity tag that owns the field.
 * @param field Entity proxy on the left.
 * @param text Prefix to look for in the linked name.
 * @return A field leaf with `op` NameStartsWith.
 */
template <typename Entity>
FieldExpr<Entity> nameStartsWith(const Field<Entity, EntityRef> &field, const std::string &text) {
    return makeComparison(field, ExprOp::NameStartsWith, Value{text});
}

/**
 * @brief Tests whether a linked entity name equals a label.
 * @tparam Entity Entity tag that owns the field.
 * @param field Entity proxy on the left.
 * @param text Exact linked name.
 * @return A field leaf with `op` NameIs.
 */
template <typename Entity>
FieldExpr<Entity> nameIs(const Field<Entity, EntityRef> &field, const std::string &text) {
    return makeComparison(field, ExprOp::NameIs, Value{text});
}

/**
 * @brief Builds an `and`, `or`, or `not` node.
 * @tparam Entity Entity tag shared by the children.
 * @param op Group operator.
 * @param args Child nodes. `not` has one, `and` and `or` have at least two.
 * @return Expression holding the new group.
 */
template <typename Entity>
FieldExpr<Entity> makeGroup(ExprOp op, std::vector<std::shared_ptr<const ExprNode>> args) {
    auto node = std::make_shared<ExprNode>();
    node->op = op;
    node->kind = ExprKind::Group;
    node->args = std::move(args);
    return FieldExpr<Entity>{std::move(node)};
}

/**
 * @brief Combines two expressions with `and`.
 *
 * Both sides are constructed before the call. There is no short-circuit.
 *
 * @tparam Entity Entity tag shared by both sides.
 * @param left Left expression.
 * @param right Right expression.
 * @return A group whose children are `left` and `right`.
 */
template <typename Entity>
FieldExpr<Entity> operator&&(const FieldExpr<Entity> &left, const FieldExpr<Entity> &right) {
    return makeGroup<Entity>(ExprOp::And, {left.node, right.node});
}

/**
 * @brief Combines two expressions with `or`.
 *
 * Both sides are constructed before the call. There is no short-circuit.
 *
 * @tparam Entity Entity tag shared by both sides.
 * @param left Left expression.
 * @param right Right expression.
 * @return A group whose children are `left` and `right`.
 */
template <typename Entity>
FieldExpr<Entity> operator||(const FieldExpr<Entity> &left, const FieldExpr<Entity> &right) {
    return makeGroup<Entity>(ExprOp::Or, {left.node, right.node});
}

/**
 * @brief Negates an expression.
 * @tparam Entity Entity tag of the expression.
 * @param expr Expression to negate.
 * @return A `not` group with `expr` as its only child.
 */
template <typename Entity> FieldExpr<Entity> operator!(const FieldExpr<Entity> &expr) {
    return makeGroup<Entity>(ExprOp::Not, {expr.node});
}

/**
 * @brief Compile-time list of parents that In() may name for an entity.
 * @tparam Parents Entity tags that appear in the YAML scope.
 */
template <typename... Parents> struct ScopeList {};

namespace detail {

/**
 * @brief True when `Needle` appears in a ScopeList pack.
 */
template <typename Needle, typename Haystack> struct scopeContains : std::false_type {};

template <typename Needle, typename... Parents>
struct scopeContains<Needle, ScopeList<Parents...>>
    : std::bool_constant<(false || ... || std::is_same_v<Needle, Parents>)> {};

} // namespace detail

/**
 * @brief Reports whether `Parent` is legal in `Entity::Scope`.
 * @tparam Entity Queried entity tag.
 * @tparam Parent Parent entity tag named by In().
 * @return True when the parent is in the entity scope.
 */
template <typename Entity, typename Parent>
constexpr bool scopeAllows() {
    return detail::scopeContains<Parent, typename Entity::Scope>::value;
}

/**
 * @brief Deep-copies an expression and stamps the domain parent on every field leaf.
 * @param root Expression root. Must not be null.
 * @param parentDomain YAML parent entity name. Must outlive the returned tree.
 * @return Shared copy with `parent` set on each Field leaf.
 */
inline std::shared_ptr<const ExprNode> stampParent(
    const std::shared_ptr<const ExprNode> &root, const char *parentDomain) {
    auto copy = std::make_shared<ExprNode>();
    copy->op = root->op;
    copy->kind = root->kind;
    copy->key = root->key;
    copy->value = root->value;
    if (root->kind == ExprKind::Field) {
        copy->parent = parentDomain;
        return copy;
    }
    copy->args.reserve(root->args.size());
    for (const auto &child : root->args) {
        copy->args.push_back(stampParent(child, parentDomain));
    }
    return copy;
}

} // namespace pms
