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
 * proxy unchanged. `==`, `!=`, and `contains()` are the field operators.
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
 * @param op `Eq`, `Ne`, or `Contains`.
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
