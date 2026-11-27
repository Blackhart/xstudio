// SPDX-License-Identifier: Apache-2.0
/**
 * @file dump.cpp
 * @brief Stable text form of an intermediate representation.
 */

#include "pms/ir.hpp"

#include "pms/error.hpp"

#include <iomanip>
#include <sstream>

namespace pms {
namespace {

/**
 * @brief Escapes quotes and backslashes so a string leaf round-trips in a dump.
 * @param text Raw string value.
 * @return The text with `"` and `\` prefixed by a backslash.
 */
std::string escape(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

/**
 * @brief Renders one filter value the way the tests compare it.
 * @param value String, int, date, list, or period.
 * @return A quoted string, a decimal integer, `YYYY-MM-DD`, `["a", "b"]`, or `7 DAY`.
 * @throw Error When the value is a links list.
 */
std::string dumpValue(const Value &value) {
    if (const auto *text = std::get_if<std::string>(&value)) {
        return "\"" + escape(*text) + "\"";
    }
    if (const auto *number = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*number);
    }
    if (const auto *texts = std::get_if<std::vector<std::string>>(&value)) {
        std::ostringstream out;
        out << '[';
        for (std::size_t i = 0; i < texts->size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            out << '"' << escape((*texts)[i]) << '"';
        }
        out << ']';
        return out.str();
    }
    if (const auto *numbers = std::get_if<std::vector<std::int64_t>>(&value)) {
        std::ostringstream out;
        out << '[';
        for (std::size_t i = 0; i < numbers->size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            out << (*numbers)[i];
        }
        out << ']';
        return out.str();
    }
    if (const auto *period = std::get_if<Period>(&value)) {
        return std::to_string(period->count) + " " + period->unit;
    }
    if (const auto *bounds = std::get_if<IntBounds>(&value)) {
        return std::to_string(bounds->low) + " .. " + std::to_string(bounds->high);
    }
    if (const auto *bounds = std::get_if<DateBounds>(&value)) {
        std::ostringstream out;
        out << std::setfill('0') << std::setw(4) << bounds->low.year << '-' << std::setw(2)
            << bounds->low.month << '-' << std::setw(2) << bounds->low.day << " .. "
            << std::setw(4) << bounds->high.year << '-' << std::setw(2) << bounds->high.month
            << '-' << std::setw(2) << bounds->high.day;
        return out.str();
    }
    if (const auto *flag = std::get_if<bool>(&value)) {
        return *flag ? "true" : "false";
    }
    if (const auto *number = std::get_if<double>(&value)) {
        return std::to_string(*number);
    }
    if (const auto *bounds = std::get_if<FloatBounds>(&value)) {
        return std::to_string(bounds->low) + " .. " + std::to_string(bounds->high);
    }
    if (const auto *link = std::get_if<Link>(&value)) {
        return link->type + ":" + std::to_string(link->id);
    }
    if (const auto *links = std::get_if<std::vector<Link>>(&value)) {
        std::ostringstream joined;
        for (std::size_t i = 0; i < links->size(); ++i) {
            if (i != 0) {
                joined << ", ";
            }
            joined << (*links)[i].type << ":" << (*links)[i].id;
        }
        return joined.str();
    }
    const Date &date = std::get<Date>(value);
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << date.year << '-' << std::setw(2) << date.month
        << '-' << std::setw(2) << date.day;
    return out.str();
}

/**
 * @brief Returns the dump token of an expression operator.
 * @param op Comparison or group operator.
 * @return The domain token, such as `eq`, `in_last`, or `and`.
 */
const char *exprOpName(ExprOp op) {
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
        return "and";
    case ExprOp::Or:
        return "or";
    case ExprOp::Not:
        return "not";
    }
    return "?";
}

/**
 * @brief Writes one node and its children at a fixed indent.
 * @param out Destination. Each node ends with a newline.
 * @param node Field leaf, scope leaf, or group.
 * @param indent Number of leading spaces.
 * @throw Error When a group child is null or a leaf has no value.
 */
void writeExpr(std::ostringstream &out, const ExprNode &node, int indent) {
    const std::string gap(indent, ' ');
    if (node.kind == ExprKind::Group) {
        out << gap << exprOpName(node.op) << '\n';
        for (const auto &child : node.args) {
            if (!child) {
                throw Error("dump: empty child");
            }
            writeExpr(out, *child, indent + 2);
        }
        return;
    }
    if (!node.value) {
        throw Error("dump: leaf \"" + node.key + "\" has no value");
    }
    out << gap << "field ";
    if (!node.parent.empty()) {
        out << node.parent << '.';
    }
    out << node.key << ' ' << exprOpName(node.op) << ' ' << dumpValue(*node.value) << '\n';
}

/**
 * @brief Writes the id, field, and value of an update.
 * @param out Destination. Each line ends with a newline.
 * @param ir Update query. `value` may be empty.
 */
void writeUpdate(std::ostringstream &out, const Ir &ir) {
    out << "id: " << ir.id << '\n';
    out << "field: " << ir.field << '\n';
    if (ir.value) {
        out << "value: " << dumpValue(*ir.value) << '\n';
    }
}

} // namespace

std::string dump(const Ir &ir) {
    std::ostringstream out;
    out << "op:";
    if (ir.op) {
        if (*ir.op == Op::Find) {
            out << " find";
        } else if (*ir.op == Op::Count) {
            out << " count";
        } else if (*ir.op == Op::Update) {
            out << " update";
        } else {
            throw Error("dump: unsupported operation");
        }
    }
    out << "\nentity: " << ir.entity << "\nin:\n";
    if (ir.in) {
        writeExpr(out, *ir.in, 2);
    }
    out << "where:\n";
    if (ir.where) {
        writeExpr(out, *ir.where, 2);
    }
    if (ir.limit) {
        out << "limit: " << *ir.limit << '\n';
    }
    if (ir.cursor) {
        out << "cursor: \"" << escape(*ir.cursor) << "\"\n";
    }
    if (ir.fields) {
        out << "fields:";
        for (const std::string &key : *ir.fields) {
            out << ' ' << key;
        }
        out << '\n';
    }
    if (ir.op && *ir.op == Op::Update) {
        writeUpdate(out, ir); ///< Id, field, and value sit after the find clauses.
    }
    return out.str();
}

} // namespace pms
