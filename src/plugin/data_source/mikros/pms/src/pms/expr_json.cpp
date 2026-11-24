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
 * @return The domain token, such as `eq`, `in_last`, or `and`.
 */
const char *opName(ExprOp op) {
    switch (op) {
    case ExprOp::Eq:
        return "eq";
    case ExprOp::Ne:
        return "ne";
    case ExprOp::Contains:
        return "contains";
    case ExprOp::NotContains:
        return "not_contains";
    case ExprOp::StartsWith:
        return "starts_with";
    case ExprOp::EndsWith:
        return "ends_with";
    case ExprOp::In:
        return "in";
    case ExprOp::NotIn:
        return "not_in";
    case ExprOp::Gt:
        return "gt";
    case ExprOp::Lt:
        return "lt";
    case ExprOp::Between:
        return "between";
    case ExprOp::InLast:
        return "in_last";
    case ExprOp::InCalendarDay:
        return "in_calendar_day";
    case ExprOp::InCalendarWeek:
        return "in_calendar_week";
    case ExprOp::InCalendarMonth:
        return "in_calendar_month";
    case ExprOp::NameContains:
        return "name_contains";
    case ExprOp::NameStartsWith:
        return "name_starts_with";
    case ExprOp::NameIs:
        return "name_is";
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
    if (text == "not_contains") {
        return ExprOp::NotContains;
    }
    if (text == "starts_with") {
        return ExprOp::StartsWith;
    }
    if (text == "ends_with") {
        return ExprOp::EndsWith;
    }
    if (text == "in") {
        return ExprOp::In;
    }
    if (text == "not_in") {
        return ExprOp::NotIn;
    }
    if (text == "gt") {
        return ExprOp::Gt;
    }
    if (text == "lt") {
        return ExprOp::Lt;
    }
    if (text == "between") {
        return ExprOp::Between;
    }
    if (text == "in_last") {
        return ExprOp::InLast;
    }
    if (text == "in_calendar_day") {
        return ExprOp::InCalendarDay;
    }
    if (text == "in_calendar_week") {
        return ExprOp::InCalendarWeek;
    }
    if (text == "in_calendar_month") {
        return ExprOp::InCalendarMonth;
    }
    if (text == "name_contains") {
        return ExprOp::NameContains;
    }
    if (text == "name_starts_with") {
        return ExprOp::NameStartsWith;
    }
    if (text == "name_is") {
        return ExprOp::NameIs;
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
 * @brief Writes one calendar day as `YYYY-MM-DD`.
 * @param date Day stored on a filter leaf.
 * @return The ISO day text.
 */
std::string formatDate(const Date &date) {
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << date.year << '-' << std::setw(2) << date.month
        << '-' << std::setw(2) << date.day;
    return out.str();
}

/**
 * @brief Reads an `in` or `not_in` list from JSON.
 * @param json Non-empty array of strings or of integers.
 * @return The list as a domain value.
 * @throw Error When the array is empty or mixes types.
 */
Value listFromJson(const nlohmann::json &json) {
    if (json.empty()) {
        throw Error("expr_json: list is empty");
    }
    if (json.front().is_string()) {
        std::vector<std::string> items;
        items.reserve(json.size());
        for (const nlohmann::json &item : json) {
            if (!item.is_string()) {
                throw Error("expr_json: list mixes types");
            }
            items.push_back(item.get<std::string>());
        }
        return items;
    }
    if (json.front().is_number_integer()) {
        std::vector<std::int64_t> items;
        items.reserve(json.size());
        for (const nlohmann::json &item : json) {
            if (!item.is_number_integer()) {
                throw Error("expr_json: list mixes types");
            }
            items.push_back(item.get<std::int64_t>());
        }
        return items;
    }
    throw Error("expr_json: list item must be a string or int");
}

/**
 * @brief Reads an `in_last` period from JSON.
 * @param json Object with integer `count` and string `unit`.
 * @return The period. The unit is checked later by validate.
 * @throw Error When `count` or `unit` is missing.
 */
Period periodFromJson(const nlohmann::json &json) {
    if (!json.contains("count") || !json["count"].is_number_integer() ||
        !json.contains("unit") || !json["unit"].is_string()) {
        throw Error("expr_json: period needs count and unit");
    }
    Period period;
    period.count = json["count"].get<std::int64_t>();
    period.unit = json["unit"].get<std::string>();
    return period;
}

/**
 * @brief Writes a filter leaf value as JSON.
 * @param value String, int, date, list, or period. Links are rejected.
 * @return JSON string, number, array, or `{count, unit}` object.
 * @throw Error When the value is a links list.
 */
nlohmann::json valueToJson(const Value &value) {
    if (const auto *text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto *number = std::get_if<std::int64_t>(&value)) {
        return *number;
    }
    if (const auto *texts = std::get_if<std::vector<std::string>>(&value)) {
        return nlohmann::json(*texts); ///< `in` on a string field.
    }
    if (const auto *numbers = std::get_if<std::vector<std::int64_t>>(&value)) {
        return nlohmann::json(*numbers); ///< `in` on an int field.
    }
    if (const auto *period = std::get_if<Period>(&value)) {
        return nlohmann::json{{"count", period->count}, {"unit", period->unit}};
    }
    if (const auto *bounds = std::get_if<IntBounds>(&value)) {
        return nlohmann::json::array({bounds->low, bounds->high}); ///< `between` on an int.
    }
    if (const auto *bounds = std::get_if<DateBounds>(&value)) {
        return nlohmann::json::array({formatDate(bounds->low), formatDate(bounds->high)});
    }
    if (std::holds_alternative<std::vector<Link>>(value)) {
        throw Error("expr_json: links are not a filter value");
    }
    return formatDate(std::get<Date>(value));
}

/**
 * @brief Reads a filter leaf value from JSON.
 * @param json String, integer, list, or period object.
 * @return Domain Value. A `YYYY-MM-DD` string stays text until validation.
 * @throw Error When the JSON type is not a filter value.
 */
Value valueFromJson(const nlohmann::json &json) {
    if (json.is_string()) {
        return json.get<std::string>();
    }
    if (json.is_number_integer()) {
        return json.get<std::int64_t>();
    }
    if (json.is_array()) {
        return listFromJson(json);
    }
    if (json.is_object()) {
        return periodFromJson(json);
    }
    throw Error("expr_json: value must be a string, int, list, or period");
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
