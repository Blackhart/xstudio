// SPDX-License-Identifier: Apache-2.0
/**
 * @file expr_json.cpp
 * @brief JSON round-trip for FieldExpr trees.
 */

#include "pms/expr_json.hpp"

#include "pms/error.hpp"

#include <iomanip>
#include <sstream>

namespace pms {
namespace {

/**
 * @brief Returns the JSON token for an expression operator.
 * @param op Comparison or group operator.
 * @return `eq`, `ne`, `contains`, `and`, `or`, or `not`.
 */
const char *opName(ExprOp op) {
    switch (op) {
    case ExprOp::Eq:
        return "eq";
    case ExprOp::Ne:
        return "ne";
    case ExprOp::Contains:
        return "contains";
    case ExprOp::And:
        return "and";
    case ExprOp::Or:
        return "or";
    case ExprOp::Not:
        return "not";
    }
    throw Error("expr_json: unknown op");
}

/**
 * @brief Parses an expression operator from a JSON string.
 * @param text Operator token.
 * @return Matching ExprOp.
 * @throw Error When the token is unknown.
 */
ExprOp opFromName(const std::string &text) {
    if (text == "eq") {
        return ExprOp::Eq;
    }
    if (text == "ne") {
        return ExprOp::Ne;
    }
    if (text == "contains") {
        return ExprOp::Contains;
    }
    if (text == "and") {
        return ExprOp::And;
    }
    if (text == "or") {
        return ExprOp::Or;
    }
    if (text == "not") {
        return ExprOp::Not;
    }
    throw Error("expr_json: unknown op \"" + text + "\"");
}

/**
 * @brief Writes a filter leaf value as JSON.
 * @param value String, int, or date. Links are rejected.
 * @return JSON string or number.
 * @throw Error When the value is a links list.
 */
nlohmann::json valueToJson(const Value &value) {
    if (const auto *text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto *number = std::get_if<std::int64_t>(&value)) {
        return *number;
    }
    if (std::holds_alternative<std::vector<Link>>(value)) {
        throw Error("expr_json: links are not a filter value");
    }
    const Date &date = std::get<Date>(value);
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << date.year << '-' << std::setw(2) << date.month
        << '-' << std::setw(2) << date.day;
    return out.str();
}

/**
 * @brief Reads a filter leaf value from JSON.
 * @param json String or integer (or unsigned integer).
 * @return Domain Value.
 * @throw Error When the JSON type is not a scalar filter value.
 */
Value valueFromJson(const nlohmann::json &json) {
    if (json.is_string()) {
        return json.get<std::string>();
    }
    if (json.is_number_integer()) {
        return json.get<std::int64_t>();
    }
    throw Error("expr_json: value must be a string or int");
}

} // namespace

nlohmann::json exprToJson(const ExprNode &node) {
    nlohmann::json out;
    out["op"] = opName(node.op);
    if (node.kind == ExprKind::Group) {
        out["kind"] = "group";
        nlohmann::json args = nlohmann::json::array();
        for (const auto &child : node.args) {
            if (!child) {
                throw Error("expr_json: empty child");
            }
            args.push_back(exprToJson(*child));
        }
        out["args"] = std::move(args);
        return out;
    }
    if (node.kind != ExprKind::Field) {
        throw Error("expr_json: unknown kind");
    }
    out["kind"] = "field";
    out["key"] = node.key;
    if (!node.parent.empty()) {
        out["parent"] = node.parent; ///< In() leaf: domain parent name.
    }
    if (!node.value) {
        throw Error("expr_json: leaf \"" + node.key + "\" has no value");
    }
    out["value"] = valueToJson(*node.value);
    return out;
}

std::shared_ptr<const ExprNode> exprFromJson(const nlohmann::json &json) {
    if (!json.is_object()) {
        throw Error("expr_json: node must be an object");
    }
    if (!json.contains("kind") || !json["kind"].is_string()) {
        throw Error("expr_json: missing kind");
    }
    if (!json.contains("op") || !json["op"].is_string()) {
        throw Error("expr_json: missing op");
    }
    const std::string kind = json["kind"].get<std::string>();
    if (kind == "scope") {
        throw Error("expr_json: scope is no longer used; use field with parent");
    }

    auto node = std::make_shared<ExprNode>();
    node->op = opFromName(json["op"].get<std::string>());

    if (kind == "group") {
        node->kind = ExprKind::Group;
        if (!json.contains("args") || !json["args"].is_array()) {
            throw Error("expr_json: group needs args array");
        }
        for (const nlohmann::json &child : json["args"]) {
            node->args.push_back(exprFromJson(child));
        }
        return node;
    }

    if (kind != "field") {
        throw Error("expr_json: unknown kind \"" + kind + "\"");
    }
    node->kind = ExprKind::Field;
    if (!json.contains("key") || !json["key"].is_string()) {
        throw Error("expr_json: field needs key");
    }
    if (!json.contains("value")) {
        throw Error("expr_json: field needs value");
    }
    node->key = json["key"].get<std::string>();
    if (json.contains("parent") && json["parent"].is_string()) {
        node->parent = json["parent"].get<std::string>();
    }
    node->value = valueFromJson(json["value"]);
    return node;
}

} // namespace pms
