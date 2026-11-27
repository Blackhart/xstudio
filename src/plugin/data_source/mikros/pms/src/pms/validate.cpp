// SPDX-License-Identifier: Apache-2.0
/**
 * @file validate.cpp
 * @brief Schema checks for a find or count query and for a returned row.
 */

#include "pms/validate.hpp"

#include "pms/error.hpp"

#include <algorithm>

namespace pms {
namespace {

/**
 * @brief Maps a leaf operator to the YAML name stored on a type.
 * @param op Operator on the expression node.
 * @return The domain token, or null when `op` is a group operator.
 */
const char *comparisonName(ExprOp op) {
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
    case ExprOp::NotBetween:
        return "not_between";
    case ExprOp::InLast:
        return "in_last";
    case ExprOp::NotInLast:
        return "not_in_last";
    case ExprOp::InNext:
        return "in_next";
    case ExprOp::NotInNext:
        return "not_in_next";
    case ExprOp::InCalendarDay:
        return "in_calendar_day";
    case ExprOp::InCalendarWeek:
        return "in_calendar_week";
    case ExprOp::InCalendarMonth:
        return "in_calendar_month";
    case ExprOp::InCalendarYear:
        return "in_calendar_year";
    case ExprOp::NameContains:
        return "name_contains";
    case ExprOp::NameNotContains:
        return "name_not_contains";
    case ExprOp::NameIs:
        return "name_is";
    case ExprOp::TypeIs:
        return "type_is";
    case ExprOp::TypeIsNot:
        return "type_is_not";
    case ExprOp::And:
    case ExprOp::Or:
    case ExprOp::Not:
        return nullptr;
    }
    return nullptr;
}

/**
 * @brief Reports whether a leaf is a calendar day.
 *
 * The C++ API stores a Date. Shelf JSON stores `YYYY-MM-DD` as text.
 *
 * @param value Leaf value.
 * @return True when `value` is a Date or a real `YYYY-MM-DD` string.
 */
bool isCalendarDay(const Value &value) {
    if (std::holds_alternative<Date>(value)) {
        return true;
    }
    const auto *text = std::get_if<std::string>(&value);
    if (!text) {
        return false;
    }
    try {
        parseDate(*text); ///< Shelf JSON stores the day as text.
        return true;
    } catch (const Error &) {
        return false;
    }
}

/**
 * @brief Reports whether a leaf is a positive in_last period.
 * @param value Leaf value.
 * @return True when count is at least 1 and the unit is one ShotGrid accepts.
 */
bool isPeriod(const Value &value) {
    const auto *period = std::get_if<Period>(&value);
    if (!period || period->count < 1) {
        return false;
    }
    return period->unit == "HOUR" || period->unit == "DAY" || period->unit == "WEEK" ||
           period->unit == "MONTH" || period->unit == "YEAR";
}

/**
 * @brief Reports whether `left` is on or before `right`.
 * @param left First day.
 * @param right Second day.
 * @return True when `left` <= `right` by year, month, day.
 */
bool dateOnOrBefore(const Date &left, const Date &right) {
    if (left.year != right.year) {
        return left.year < right.year;
    }
    if (left.month != right.month) {
        return left.month < right.month;
    }
    return left.day <= right.day;
}

/**
 * @brief Reads a calendar day from a Date or a `YYYY-MM-DD` string.
 * @param value Leaf value.
 * @param out Filled when the value is a calendar day.
 * @return True when `out` was set.
 */
bool asCalendarDay(const Value &value, Date &out) {
    if (const auto *date = std::get_if<Date>(&value)) {
        out = *date;
        return true;
    }
    const auto *text = std::get_if<std::string>(&value);
    if (!text) {
        return false;
    }
    try {
        out = parseDate(*text);
        return true;
    } catch (const Error &) {
        return false;
    }
}

/**
 * @brief Reports whether a between leaf has ordered int bounds.
 *
 * Accepts IntBounds from the C++ builder, or a two-int list from shelf JSON.
 *
 * @param value Leaf value.
 * @return True when low <= high.
 */
bool isIntBetween(const Value &value) {
    if (const auto *bounds = std::get_if<IntBounds>(&value)) {
        return bounds->low <= bounds->high;
    }
    const auto *items = std::get_if<std::vector<std::int64_t>>(&value);
    return items && items->size() == 2 && (*items)[0] <= (*items)[1];
}

/**
 * @brief Reports whether a between leaf has ordered calendar-day bounds.
 *
 * Accepts DateBounds from the C++ builder, or two day strings from shelf JSON.
 *
 * @param value Leaf value.
 * @return True when both bounds are days and low <= high.
 */
bool isDateBetween(const Value &value) {
    if (const auto *bounds = std::get_if<DateBounds>(&value)) {
        return dateOnOrBefore(bounds->low, bounds->high);
    }
    const auto *items = std::get_if<std::vector<std::string>>(&value);
    if (!items || items->size() != 2) {
        return false;
    }
    Date low;
    Date high;
    if (!asCalendarDay(Value{(*items)[0]}, low) || !asCalendarDay(Value{(*items)[1]}, high)) {
        return false;
    }
    return dateOnOrBefore(low, high);
}

/**
 * @brief Reports whether a between leaf has ordered float bounds.
 * @param value Leaf value. A two-number list from shelf JSON is accepted.
 * @return True when low <= high.
 */
bool isFloatBetween(const Value &value) {
    if (const auto *bounds = std::get_if<FloatBounds>(&value)) {
        return bounds->low <= bounds->high;
    }
    const auto *items = std::get_if<std::vector<double>>(&value);
    if (items && items->size() == 2) {
        return (*items)[0] <= (*items)[1];
    }
    const auto *ints = std::get_if<std::vector<std::int64_t>>(&value);
    return ints && ints->size() == 2 && (*ints)[0] <= (*ints)[1];
}

/**
 * @brief Reports whether `type` stores text.
 * @param type ShotGrid data type.
 * @return True for text, list, and status_list.
 */
bool isTextType(const std::string &type) {
    return type == "text" || type == "list" || type == "status_list";
}

/**
 * @brief Reports whether a link leaf names an id and a type.
 * @param link Filter link.
 * @return True when both are present.
 */
bool linkIdentityOk(const Link &link) { return link.id != 0 && !link.type.empty(); }

/**
 * @brief Reports whether a leaf value matches the operator and the ShotGrid type.
 * @param type ShotGrid data type copied from the mapping.
 * @param op Comparison operator already allowed on that type.
 * @param value Leaf value.
 * @return True when the shape is legal for that pair.
 */
bool leafValueOk(const std::string &type, ExprOp op, const Value &value) {
    const bool linkType = type == "entity" || type == "multi_entity";
    if (op == ExprOp::In || op == ExprOp::NotIn) {
        if (isTextType(type) || type == "date_time") {
            const auto *items = std::get_if<std::vector<std::string>>(&value);
            return items && !items->empty();
        }
        if (type == "number") {
            const auto *items = std::get_if<std::vector<std::int64_t>>(&value);
            return items && !items->empty();
        }
        if (type == "float") {
            const auto *items = std::get_if<std::vector<double>>(&value);
            if (items && !items->empty()) {
                return true;
            }
            const auto *ints = std::get_if<std::vector<std::int64_t>>(&value);
            return ints && !ints->empty();
        }
        if (linkType) {
            const auto *items = std::get_if<std::vector<Link>>(&value);
            if (!items || items->empty()) {
                return false;
            }
            for (const Link &link : *items) {
                if (!linkIdentityOk(link)) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }
    if (op == ExprOp::Between || op == ExprOp::NotBetween) {
        if (type == "number") {
            return isIntBetween(value);
        }
        if (type == "float") {
            return isFloatBetween(value);
        }
        if (type == "date_time") {
            return isDateBetween(value);
        }
        return false;
    }
    if (op == ExprOp::InLast || op == ExprOp::NotInLast || op == ExprOp::InNext ||
        op == ExprOp::NotInNext) {
        return type == "date_time" && isPeriod(value);
    }
    if (op == ExprOp::InCalendarDay || op == ExprOp::InCalendarWeek ||
        op == ExprOp::InCalendarMonth || op == ExprOp::InCalendarYear) {
        return type == "date_time" && std::holds_alternative<std::int64_t>(value);
    }
    if (op == ExprOp::NameContains || op == ExprOp::NameNotContains || op == ExprOp::NameIs ||
        op == ExprOp::TypeIs || op == ExprOp::TypeIsNot) {
        const auto *text = std::get_if<std::string>(&value);
        return linkType && text && !text->empty();
    }
    if (type == "date_time") {
        return isCalendarDay(value);
    }
    if (isTextType(type)) {
        return std::holds_alternative<std::string>(value);
    }
    if (type == "number" || type == "summary") {
        return std::holds_alternative<std::int64_t>(value);
    }
    if (type == "float") {
        return std::holds_alternative<double>(value) ||
               std::holds_alternative<std::int64_t>(value);
    }
    if (type == "checkbox") {
        return std::holds_alternative<bool>(value);
    }
    if (linkType) {
        const auto *link = std::get_if<Link>(&value);
        return link && linkIdentityOk(*link);
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
    if (!node.value || !leafValueOk(spec->type, node.op, *node.value)) {
        throw Error("where: type mismatch on \"" + node.key + "\"");
    }
}

/**
 * @brief Checks one In-node and its children against parent field rules.
 * @param node Field leaf (stamped with `parent`) or group.
 * @param entity Queried entity whose YAML scope lists legal parents.
 * @param schema Schema used to inherit ancestor parents and resolve parent fields.
 * @throw Error When the shape, the parent, the field, the operator, or the value is illegal.
 */
void checkIn(const ExprNode &node, const EntitySpec &entity, const Schema &schema) {
    if (node.kind == ExprKind::Group) {
        checkGroupShape(node, "in");
        for (const auto &child : node.args) {
            checkIn(*child, entity, schema);
        }
        return;
    }

    if (node.kind != ExprKind::Field) {
        throw Error("in: invalid node");
    }
    if (node.parent.empty()) {
        throw Error("in: field \"" + node.key + "\" has no parent");
    }
    if (!schema.allowsInParent(entity, node.parent)) {
        throw Error("in: parent \"" + node.parent + "\" is not in scope");
    }
    const EntitySpec *parent = schema.entity(node.parent);
    if (!parent) {
        throw Error("in: unknown parent \"" + node.parent + "\"");
    }
    const char *opName = comparisonName(node.op);
    if (!opName) {
        throw Error("in: operator not allowed on \"" + node.parent + "." + node.key + "\"");
    }
    const FieldSpec *spec = parent->field(node.key);
    if (!spec) {
        throw Error("in: unknown field \"" + node.parent + "." + node.key + "\"");
    }
    if (!spec->filter) {
        throw Error("in: field \"" + node.parent + "." + node.key + "\" is not filterable");
    }
    if (std::find(spec->ops.begin(), spec->ops.end(), opName) == spec->ops.end()) {
        throw Error("in: operator not allowed on \"" + node.parent + "." + node.key + "\"");
    }
    if (!node.value || !leafValueOk(spec->type, node.op, *node.value)) {
        throw Error("in: type mismatch on \"" + node.parent + "." + node.key + "\"");
    }
}

} // namespace

void validate(const Ir &ir, const Schema &schema) {
    if (!ir.op) {
        throw Error("missing operation");
    }
    if (*ir.op != Op::Find && *ir.op != Op::Count) {
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
    if (*ir.op == Op::Count) {
        if (ir.limit) {
            throw Error("count does not accept a limit");
        }
        if (ir.cursor) {
            throw Error("count does not accept a cursor");
        }
        return;
    }
    if (ir.limit && *ir.limit == 0) {
        throw Error("limit must be greater than zero");
    }
    if (ir.cursor && !ir.limit) {
        throw Error("cursor requires a limit");
    }
    if (ir.fields) {
        if (ir.fields->empty()) {
            throw Error("fields must list at least one key");
        }
        for (const std::string &key : *ir.fields) {
            if (!entity->field(key)) {
                throw Error("fields: unknown field \"" + key + "\"");
            }
        }
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
