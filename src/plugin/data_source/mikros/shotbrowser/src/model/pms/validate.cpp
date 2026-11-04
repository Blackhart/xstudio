// SPDX-License-Identifier: Apache-2.0
/**
 * @file validate.cpp
 * @brief Schema checks for a find query and for a returned row.
 */

#include "pms/validate.hpp"

#include "pms/error.hpp"

#include <algorithm>

namespace pms {
namespace {

/**
 * @brief Maps a leaf operator to the YAML name stored on a type.
 * @param op Operator on the expression node.
 * @return `"eq"` or `"ne"`, or null when `op` is a group operator.
 */
const char *comparisonName(ExprOp op) {
    switch (op) {
    case ExprOp::Eq:
        return "eq";
    case ExprOp::Ne:
        return "ne";
    case ExprOp::And:
    case ExprOp::Or:
    case ExprOp::Not:
        return nullptr;
    }
    return nullptr;
}

/**
 * @brief Reports whether a leaf value has the C++ type named by the schema.
 * @param type YAML type name: `string`, `int`, or `date`.
 * @param value Scalar stored on the leaf.
 * @return True when the alternative in `value` matches `type`.
 */
bool valueMatches(const std::string &type, const Value &value) {
    if (type == "string") {
        return std::holds_alternative<std::string>(value);
    }
    if (type == "int") {
        return std::holds_alternative<std::int64_t>(value);
    }
    if (type == "date") {
        return std::holds_alternative<Date>(value);
    }
    return false;
}

/**
 * @brief Checks the shape of a group node.
 * @param node Group node.
 * @param clause Name used in error messages (`in` or `where`).
 * @throw Error When the child count is illegal.
 */
void checkGroupShape(const ExprNode &node, const char *clause) {
    if (node.op == ExprOp::Not) {
        if (node.args.size() != 1 || !node.args[0]) {
            throw Error(std::string(clause) + ": not expects one child");
        }
        return;
    }
    if (node.op == ExprOp::And || node.op == ExprOp::Or) {
        if (node.args.size() < 2) {
            const char *name = node.op == ExprOp::And ? "and" : "or";
            throw Error(std::string(clause) + ": " + name + " expects at least two children");
        }
        for (const auto &child : node.args) {
            if (!child) {
                throw Error(std::string(clause) + ": empty child");
            }
        }
        return;
    }
    throw Error(std::string(clause) + ": invalid group");
}

/**
 * @brief Checks one where-node and its children against an entity.
 * @param node Field leaf or group. Walked depth-first.
 * @param entity Entity the field keys must belong to.
 * @throw Error When the shape, the field, the operator, or the value type is illegal.
 */
void checkWhere(const ExprNode &node, const EntitySpec &entity) {
    if (node.kind == ExprKind::Group) {
        checkGroupShape(node, "where");
        for (const auto &child : node.args) {
            checkWhere(*child, entity);
        }
        return;
    }

    if (node.kind != ExprKind::Field) {
        throw Error("where: invalid node");
    }
    const char *opName = comparisonName(node.op);
    if (!opName) {
        throw Error("where: operator not allowed on \"" + node.key + "\"");
    }
    const FieldSpec *spec = entity.field(node.key);
    if (!spec) {
        throw Error("where: unknown field \"" + node.key + "\"");
    }
    if (!spec->filter) {
        throw Error("where: field \"" + node.key + "\" is not filterable");
    }
    if (std::find(spec->ops.begin(), spec->ops.end(), opName) == spec->ops.end()) {
        throw Error("where: operator not allowed on \"" + node.key + "\"");
    }
    if (!node.value || !valueMatches(spec->type, *node.value)) {
        throw Error("where: type mismatch on \"" + node.key + "\"");
    }
}

/**
 * @brief Checks one In-node and its children against an entity scope.
 * @param node Scope leaf or group.
 * @param entity Entity whose YAML scope lists direct parents.
 * @param schema Schema used to inherit ancestor parents.
 * @throw Error When the shape, the parent, the operator, or the value is illegal.
 */
void checkIn(const ExprNode &node, const EntitySpec &entity, const Schema &schema) {
    if (node.kind == ExprKind::Group) {
        checkGroupShape(node, "in");
        for (const auto &child : node.args) {
            checkIn(*child, entity, schema);
        }
        return;
    }

    if (node.kind != ExprKind::Scope) {
        throw Error("in: invalid node");
    }
    if (node.op != ExprOp::Eq && node.op != ExprOp::Ne) {
        throw Error("in: operator not allowed on \"" + node.key + "\"");
    }
    if (!schema.allowsInParent(entity, node.key)) {
        throw Error("in: parent \"" + node.key + "\" is not in scope");
    }
    if (!node.value ||
        (!std::holds_alternative<std::string>(*node.value) &&
         !std::holds_alternative<std::int64_t>(*node.value))) {
        throw Error("in: parent \"" + node.key + "\" expects an id or a name");
    }
}

} // namespace

void validate(const Ir &ir, const Schema &schema) {
    if (!ir.op || *ir.op != Op::Find) {
        throw Error("missing operation");
    }
    const EntitySpec *entity = schema.entity(ir.entity);
    if (!entity) {
        throw Error("unknown entity \"" + ir.entity + "\"");
    }
    if (ir.in) {
        checkIn(*ir.in, *entity, schema);
    }
    if (ir.where) {
        checkWhere(*ir.where, *entity);
    }
    if (ir.limit && *ir.limit == 0) {
        throw Error("limit must be greater than zero");
    }
    if (ir.cursor && !ir.limit) {
        throw Error("cursor requires a limit");
    }
}

void checkRecord(const Record &record) {
    const auto id = record.fields.find("id");
    if (id == record.fields.end()) {
        throw Error("record missing \"id\"");
    }
    if (!std::holds_alternative<std::int64_t>(id->second)) {
        throw Error("record \"id\" is not an int");
    }
    const auto name = record.fields.find("name");
    if (name == record.fields.end()) {
        throw Error("record missing \"name\"");
    }
    if (!std::holds_alternative<std::string>(name->second)) {
        throw Error("record \"name\" is not a string");
    }
}

} // namespace pms
