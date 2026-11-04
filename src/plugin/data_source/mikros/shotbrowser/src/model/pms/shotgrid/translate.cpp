// SPDX-License-Identifier: Apache-2.0
/**
 * @file shotgrid/translate.cpp
 * @brief Builds ShotGrid filters and maps response rows to domain records.
 */

#include "pms/shotgrid/translate.hpp"

#include "pms/error.hpp"

#include <iomanip>
#include <queue>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace pms {
namespace {

const char *comparisonOp(ExprOp op) {
    switch (op) {
    case ExprOp::Eq:
        return "is";
    case ExprOp::Ne:
        return "is_not";
    default:
        return nullptr;
    }
}

nlohmann::json valueJson(const Value &value) {
    if (const auto *text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto *number = std::get_if<std::int64_t>(&value)) {
        return *number;
    }
    const Date &date = std::get<Date>(value);
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << date.year << '-' << std::setw(2) << date.month
        << '-' << std::setw(2) << date.day;
    return out.str();
}

struct LinkHop {
    std::string field;
    std::string type_name;
    std::string target;
};

std::vector<LinkHop> linkPathTo(
    const ShotGridMapping &mapping, const std::string &from, const std::string &parent) {
    const ShotGridEntityMap *start = mapping.entity(from);
    if (!start) {
        throw Error("shotgrid: unknown entity \"" + from + "\"");
    }
    if (const ShotGridLinkMap *direct = start->link(parent)) {
        const ShotGridEntityMap *target = mapping.entity(direct->target);
        if (!target || target->shotgrid.empty()) {
            throw Error("shotgrid: incomplete target for link \"" + parent + "\"");
        }
        return {LinkHop{direct->shotgrid, target->shotgrid, direct->target}};
    }

    struct Node {
        std::string domain;
        std::vector<LinkHop> path;
    };
    std::queue<Node> queue;
    std::unordered_set<std::string> seen;
    queue.push(Node{from, {}});
    seen.insert(from);
    while (!queue.empty()) {
        Node node = std::move(queue.front());
        queue.pop();
        const ShotGridEntityMap *entity = mapping.entity(node.domain);
        if (!entity) {
            continue;
        }
        for (const ShotGridLinkMap &link : entity->links) {
            if (seen.count(link.target) != 0) {
                continue;
            }
            const ShotGridEntityMap *target = mapping.entity(link.target);
            if (!target || target->shotgrid.empty() || link.shotgrid.empty()) {
                continue;
            }
            std::vector<LinkHop> next = node.path;
            next.push_back(LinkHop{link.shotgrid, target->shotgrid, link.target});
            if (link.target == parent) {
                return next;
            }
            seen.insert(link.target);
            queue.push(Node{link.target, std::move(next)});
        }
    }
    throw Error("shotgrid: no link path from \"" + from + "\" to parent \"" + parent + "\"");
}

std::string pathField(const std::vector<LinkHop> &hops, const std::string &leaf = {}) {
    std::ostringstream out;
    for (std::size_t i = 0; i < hops.size(); ++i) {
        if (i == 0) {
            out << hops[i].field;
        } else {
            out << '.' << hops[i - 1].type_name << '.' << hops[i].field;
        }
    }
    if (!leaf.empty()) {
        out << '.' << hops.back().type_name << '.' << leaf;
    }
    return out.str();
}

nlohmann::json scopeCondition(
    const ExprNode &node, const ShotGridMapping &mapping, const std::string &entity) {
    const char *op = comparisonOp(node.op);
    if (!op || !node.value) {
        throw Error("shotgrid: invalid scope leaf on \"" + node.key + "\"");
    }
    const std::vector<LinkHop> hops = linkPathTo(mapping, entity, node.key);
    if (const auto *id = std::get_if<std::int64_t>(&*node.value)) {
        nlohmann::json condition = nlohmann::json::array();
        condition.push_back(pathField(hops));
        condition.push_back(op);
        condition.push_back(nlohmann::json{{"type", hops.back().type_name}, {"id", *id}});
        return condition;
    }
    if (const auto *name = std::get_if<std::string>(&*node.value)) {
        const ShotGridEntityMap *target = mapping.entity(hops.back().target);
        const ShotGridFieldMap *nameField = target ? target->field("name") : nullptr;
        if (!nameField || nameField->shotgrid.empty()) {
            throw Error("shotgrid: parent \"" + node.key + "\" has no mapped name field");
        }
        nlohmann::json condition = nlohmann::json::array();
        condition.push_back(pathField(hops, nameField->shotgrid));
        condition.push_back(op);
        condition.push_back(*name);
        return condition;
    }
    throw Error("shotgrid: scope \"" + node.key + "\" expects id or name");
}

nlohmann::json fieldCondition(const ExprNode &node, const ShotGridEntityMap &entity) {
    const char *op = comparisonOp(node.op);
    if (!op || !node.value) {
        throw Error("shotgrid: invalid field leaf on \"" + node.key + "\"");
    }
    const ShotGridFieldMap *field = entity.field(node.key);
    if (!field || field->shotgrid.empty()) {
        throw Error("shotgrid: field \"" + entity.domain + "." + node.key + "\" is not mapped");
    }
    return nlohmann::json::array({field->shotgrid, op, valueJson(*node.value)});
}

nlohmann::json exprToJson(
    const ExprNode &node,
    const ShotGridMapping &mapping,
    const ShotGridEntityMap &entity,
    bool scope) {
    if (node.kind == ExprKind::Group) {
        if (node.op == ExprOp::Not) {
            if (node.args.size() != 1 || !node.args[0]) {
                throw Error("shotgrid: not expects one child");
            }
            nlohmann::json child = exprToJson(*node.args[0], mapping, entity, scope);
            if (!child.is_array() || child.size() < 2) {
                throw Error("shotgrid: not only inverts a leaf");
            }
            const std::string op = child[1].get<std::string>();
            child[1] = (op == "is") ? "is_not" : "is";
            return child;
        }
        if (node.op != ExprOp::And && node.op != ExprOp::Or) {
            throw Error("shotgrid: invalid group");
        }
        nlohmann::json out;
        out["logical_operator"] = node.op == ExprOp::And ? "and" : "or";
        out["conditions"] = nlohmann::json::array();
        for (const auto &child : node.args) {
            if (!child) {
                throw Error("shotgrid: empty child");
            }
            out["conditions"].push_back(exprToJson(*child, mapping, entity, scope));
        }
        return out;
    }
    if (scope) {
        return scopeCondition(node, mapping, entity.domain);
    }
    return fieldCondition(node, entity);
}

nlohmann::json andCombine(nlohmann::json left, nlohmann::json right) {
    auto asConditions = [](nlohmann::json node) {
        if (node.is_object() && node.value("logical_operator", "") == "and" &&
            node.contains("conditions")) {
            return node["conditions"];
        }
        return nlohmann::json::array({std::move(node)});
    };
    nlohmann::json out;
    out["logical_operator"] = "and";
    out["conditions"] = asConditions(std::move(left));
    for (auto &part : asConditions(std::move(right))) {
        out["conditions"].push_back(std::move(part));
    }
    return out;
}

bool looksLikeDateField(const std::string &domain) {
    return domain.find("date") != std::string::npos;
}

} // namespace

nlohmann::json shotgridFiltersFromIr(const Ir &ir, const ShotGridMapping &mapping) {
    const ShotGridEntityMap *entity = mapping.entity(ir.entity);
    if (!entity || entity->shotgrid.empty()) {
        throw Error("shotgrid: entity \"" + ir.entity + "\" is not mapped");
    }
    nlohmann::json filters = nlohmann::json::object();
    if (ir.in) {
        filters = exprToJson(*ir.in, mapping, *entity, true);
    }
    if (ir.where) {
        nlohmann::json where = exprToJson(*ir.where, mapping, *entity, false);
        filters =
            filters.empty() ? std::move(where) : andCombine(std::move(filters), std::move(where));
    }
    if (ir.limit && ir.cursor) {
        std::int64_t afterId = 0;
        try {
            afterId = std::stoll(*ir.cursor);
        } catch (const std::exception &) {
            throw Error("shotgrid: cursor must be an integer id");
        }
        const ShotGridFieldMap *idField = entity->field("id");
        if (!idField) {
            throw Error("shotgrid: entity \"" + ir.entity + "\" has no id field");
        }
        nlohmann::json cursorFilter =
            nlohmann::json::array({idField->shotgrid, "greater_than", afterId});
        filters = filters.empty()
                      ? nlohmann::json{
                            {"logical_operator", "and"},
                            {"conditions", nlohmann::json::array({cursorFilter})}}
                      : andCombine(std::move(filters), std::move(cursorFilter));
    }
    return filters;
}

std::vector<std::string> shotgridFieldNames(const ShotGridEntityMap &entity) {
    std::vector<std::string> names;
    names.reserve(entity.fields.size());
    for (const ShotGridFieldMap &field : entity.fields) {
        if (!field.shotgrid.empty()) {
            names.push_back(field.shotgrid);
        }
    }
    return names;
}

Record shotgridRecordFromJson(const nlohmann::json &row, const ShotGridEntityMap &entity) {
    const nlohmann::json *attributes = &row;
    if (row.contains("attributes") && row["attributes"].is_object()) {
        attributes = &row["attributes"];
    }
    Record out;
    for (const ShotGridFieldMap &field : entity.fields) {
        if (field.shotgrid.empty()) {
            continue;
        }
        if (field.domain == "id") {
            std::int64_t id = 0;
            if (row.contains("id") && row["id"].is_number_integer()) {
                id = row["id"].get<std::int64_t>();
            } else if (attributes->contains(field.shotgrid) &&
                       (*attributes)[field.shotgrid].is_number_integer()) {
                id = (*attributes)[field.shotgrid].get<std::int64_t>();
            } else {
                throw Error("shotgrid: row missing id");
            }
            out.fields["id"] = id;
            continue;
        }
        if (!attributes->contains(field.shotgrid) || (*attributes)[field.shotgrid].is_null()) {
            continue;
        }
        const nlohmann::json &value = (*attributes)[field.shotgrid];
        if (value.is_number_integer()) {
            out.fields[field.domain] = value.get<std::int64_t>();
        } else if (value.is_string()) {
            const std::string text = value.get<std::string>();
            if (looksLikeDateField(field.domain)) {
                out.fields[field.domain] = pms::parseDate(text);
            } else {
                out.fields[field.domain] = text;
            }
        }
    }
    if (out.fields.find("id") == out.fields.end()) {
        throw Error("shotgrid: record missing id");
    }
    if (out.fields.find("name") == out.fields.end()) {
        throw Error("shotgrid: record missing name");
    }
    return out;
}

} // namespace pms
