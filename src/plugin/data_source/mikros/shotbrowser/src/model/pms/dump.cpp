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
 * @brief Renders one scalar the way the tests compare it.
 * @param value String, int, or date.
 * @return A quoted string, a decimal integer, or `YYYY-MM-DD`.
 */
std::string dumpValue(const Value &value) {
    if (const auto *text = std::get_if<std::string>(&value)) {
        return "\"" + escape(*text) + "\"";
    }
    if (const auto *number = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*number);
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
 * @return `eq`, `ne`, `and`, `or`, or `not`.
 */
const char *exprOpName(ExprOp op) {
    switch (op) {
    case ExprOp::Eq:
        return "eq";
    case ExprOp::Ne:
        return "ne";
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
    const char *kind = node.kind == ExprKind::Scope ? "scope" : "field";
    out << gap << kind << ' ' << node.key << ' ' << exprOpName(node.op) << ' '
        << dumpValue(*node.value) << '\n';
}

} // namespace

std::string dump(const Ir &ir) {
    std::ostringstream out;
    out << "op:";
    if (ir.op) {
        if (*ir.op != Op::Find) {
            throw Error("dump: unsupported operation");
        }
        out << " find";
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
    return out.str();
}

} // namespace pms
