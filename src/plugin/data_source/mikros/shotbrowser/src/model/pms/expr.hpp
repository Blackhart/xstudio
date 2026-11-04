// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/expr.hpp
 * @brief Field proxies and the operators that build a where-tree.
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
 * the argument convert to it. `status == "ip"` therefore builds a string leaf.
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
 * proxy unchanged. `==` and `!=` are the only operators in this slice.
 *
 * @tparam Entity Entity tag that owns the field.
 * @tparam T C++ type of the field value.
 */
template <typename Entity, typename T> struct Field {
    const char *key = nullptr; ///< YAML field name, such as `"start_date"`.

    /**
     * @brief Binds the proxy to a YAML key.
     * @param fieldKey YAML field name. Must outlive the proxy.
     */
    constexpr explicit Field(const char *fieldKey) : key(fieldKey) {}
};

/**
 * @brief Where-tree whose leaves all belong to one entity.
 *
 * `&&`, `||`, and `!` combine expressions of the same entity. They build both
 * sides: the overloaded operators do not short-circuit. C++ precedence applies,
 * so parentheses are significant.
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
 * @param op `Eq` or `Ne`.
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
 * @tparam Parent Parent entity tag named by a ScopeRef.
 * @return True when the parent is in the entity scope.
 */
template <typename Entity, typename Parent>
constexpr bool scopeAllows() {
    return detail::scopeContains<Parent, typename Entity::Scope>::value;
}

/**
 * @brief Parent-name expression built by In().
 *
 * The parent pack is part of the type so Query::In can reject illegal parents
 * at compile time. The node itself is type-erased for storage on Ir.
 *
 * @tparam Parents Parent entity tags named by this expression.
 */
template <typename... Parents> struct ScopeExpr {
    std::shared_ptr<const ExprNode> node; ///< Root of this expression.
};

/**
 * @brief Compile-time name of one legal parent for In().
 * @tparam Parent Entity tag of the parent.
 */
template <typename Parent> struct ScopeRef {
    const char *key = nullptr; ///< YAML entity name of the parent.

    /**
     * @brief Binds the proxy to a parent entity key.
     * @param parentKey YAML entity name. Must outlive the proxy.
     */
    constexpr explicit ScopeRef(const char *parentKey) : key(parentKey) {}
};

/**
 * @brief Builds a scope comparison leaf.
 * @tparam Parent Parent entity tag.
 * @param parent Proxy that supplies the YAML parent key.
 * @param op `Eq` or `Ne`.
 * @param value Parent `id` (int) or parent `name` (string).
 * @return Expression holding the new scope node.
 */
template <typename Parent>
ScopeExpr<Parent> makeScopeComparison(
    const ScopeRef<Parent> &parent, ExprOp op, Value value) {
    auto node = std::make_shared<ExprNode>();
    node->op = op;
    node->kind = ExprKind::Scope;
    node->key = parent.key;
    node->value = std::move(value);
    return ScopeExpr<Parent>{std::move(node)};
}

/**
 * @brief Matches a parent by name.
 * @tparam Parent Parent entity tag.
 * @param parent Scope proxy on the left.
 * @param name Parent `name`.
 * @return A scope leaf with `op` Eq.
 */
template <typename Parent>
ScopeExpr<Parent> operator==(const ScopeRef<Parent> &parent, const std::string &name) {
    return makeScopeComparison(parent, ExprOp::Eq, Value{name});
}

/**
 * @brief Rejects a parent by name.
 * @tparam Parent Parent entity tag.
 * @param parent Scope proxy on the left.
 * @param name Parent `name`.
 * @return A scope leaf with `op` Ne.
 */
template <typename Parent>
ScopeExpr<Parent> operator!=(const ScopeRef<Parent> &parent, const std::string &name) {
    return makeScopeComparison(parent, ExprOp::Ne, Value{name});
}

/**
 * @brief Matches a parent by id.
 * @tparam Parent Parent entity tag.
 * @param parent Scope proxy on the left.
 * @param id Parent `id`.
 * @return A scope leaf with `op` Eq.
 */
template <typename Parent>
ScopeExpr<Parent> operator==(const ScopeRef<Parent> &parent, std::int64_t id) {
    return makeScopeComparison(parent, ExprOp::Eq, Value{id});
}

/**
 * @brief Rejects a parent by id.
 * @tparam Parent Parent entity tag.
 * @param parent Scope proxy on the left.
 * @param id Parent `id`.
 * @return A scope leaf with `op` Ne.
 */
template <typename Parent>
ScopeExpr<Parent> operator!=(const ScopeRef<Parent> &parent, std::int64_t id) {
    return makeScopeComparison(parent, ExprOp::Ne, Value{id});
}

/**
 * @brief Builds an `and`, `or`, or `not` scope group.
 * @tparam Parents Combined parent pack of the children.
 * @param op Group operator.
 * @param args Child nodes.
 * @return Expression holding the new group.
 */
template <typename... Parents>
ScopeExpr<Parents...> makeScopeGroup(
    ExprOp op, std::vector<std::shared_ptr<const ExprNode>> args) {
    auto node = std::make_shared<ExprNode>();
    node->op = op;
    node->kind = ExprKind::Group;
    node->args = std::move(args);
    return ScopeExpr<Parents...>{std::move(node)};
}

/**
 * @brief Combines two scope expressions with `and`.
 * @tparam A Parents of the left expression.
 * @tparam B Parents of the right expression.
 */
template <typename... A, typename... B>
ScopeExpr<A..., B...> operator&&(const ScopeExpr<A...> &left, const ScopeExpr<B...> &right) {
    return makeScopeGroup<A..., B...>(ExprOp::And, {left.node, right.node});
}

/**
 * @brief Combines two scope expressions with `or`.
 * @tparam A Parents of the left expression.
 * @tparam B Parents of the right expression.
 */
template <typename... A, typename... B>
ScopeExpr<A..., B...> operator||(const ScopeExpr<A...> &left, const ScopeExpr<B...> &right) {
    return makeScopeGroup<A..., B...>(ExprOp::Or, {left.node, right.node});
}

/**
 * @brief Negates a scope expression.
 * @tparam Parents Parent pack of the expression.
 */
template <typename... Parents>
ScopeExpr<Parents...> operator!(const ScopeExpr<Parents...> &expr) {
    return makeScopeGroup<Parents...>(ExprOp::Not, {expr.node});
}

} // namespace pms
