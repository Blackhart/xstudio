// SPDX-License-Identifier: Apache-2.0
/**
 * @file shotgrid/translate.cpp
 * @brief Builds ShotGrid filters and maps response rows to domain records.
 */

#include "pms/shotgrid/translate.hpp"

#include "pms/error.hpp"

#include <algorithm>
#include <iomanip>
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

/**
 * @brief True when exactly one link on `schema` uses `shotgridField`.
 * @param schema Entity whose links are counted.
 * @param shotgridField ShotGrid field name on those links.
 * @return True when that field is not shared with another parent link.
 */
bool linkFieldIsExclusive(const ShotGridSchema &schema, const std::string &shotgridField) {
    std::size_t count = 0;
    for (const ShotGridSchemaLink &link : schema.links) {
        if (link.shotgrid == shotgridField) {
            ++count;
        }
    }
    return count == 1;
}

/**
 * @brief Collects ownership paths from `from` to `parent`.
 *
 * When a one-hop path uses a ShotGrid field that no other link on `from`
 * shares, only those exclusive direct paths are kept (version in project via
 * `project`). Otherwise every path is kept, so a version in a sequence still
 * includes versions whose entity is that sequence and versions whose entity is
 * a shot of that sequence. Playlist membership is not an ownership bridge: the
 * playlist link is used only when the parent is the playlist itself.
 *
 * @param mapping Domain ↔ ShotGrid map.
 * @param from Entity being queried, such as `"version"`.
 * @param parent Scope parent, such as `"project"`.
 * @return Paths in mapping order. Each path is one or more hops.
 * @throw Error When `from` is unknown, a link target is incomplete, or no path exists.
 */
std::vector<std::vector<LinkHop>> linkPathsTo(
    const ShotGridMapping &mapping, const std::string &from, const std::string &parent) {
    const ShotGridSchema *fromSchema = mapping.findSchema(from);
    if (!fromSchema) {
        throw Error("shotgrid: unknown entity \"" + from + "\"");
    }

    std::vector<std::vector<LinkHop>> found;
    std::vector<LinkHop> path;
    std::unordered_set<std::string> onPath;

    struct Walk {
        const ShotGridMapping &mapping;
        const std::string &parent;
        std::vector<std::vector<LinkHop>> &found;
        std::vector<LinkHop> &path;
        std::unordered_set<std::string> &onPath;

        void visit(const std::string &domain) {
            if (!onPath.insert(domain).second) {
                return; ///< A cycle in the mapping is not another ownership path.
            }
            const ShotGridSchema *schema = mapping.findSchema(domain);
            if (!schema) {
                onPath.erase(domain);
                return;
            }
            for (const ShotGridSchemaLink &link : schema->links) {
                const bool reachesParent = link.shotgridTargetName == parent;
                if (!reachesParent && link.shotgridTargetName == "playlist") {
                    continue; ///< Playlist membership does not place a version in a project.
                }
                const ShotGridSchema *target = mapping.findSchema(link.shotgridTargetName);
                if (link.shotgrid.empty() || !target || target->shotgridName.empty()) {
                    throw Error("shotgrid: incomplete target for link \"" + link.domain + "\"");
                }
                path.push_back(
                    LinkHop{link.shotgrid, target->shotgridName, link.shotgridTargetName});
                if (reachesParent) {
                    found.push_back(path);
                } else {
                    visit(link.shotgridTargetName);
                }
                path.pop_back();
            }
            onPath.erase(domain);
        }
    };

    Walk walk{mapping, parent, found, path, onPath};
    walk.visit(from);
    if (found.empty()) {
        throw Error("shotgrid: no link path from \"" + from + "\" to parent \"" + parent + "\"");
    }

    std::vector<std::vector<LinkHop>> exclusiveDirect;
    for (const std::vector<LinkHop> &hops : found) {
        if (hops.size() == 1 && linkFieldIsExclusive(*fromSchema, hops.front().field)) {
            exclusiveDirect.push_back(hops); ///< Dedicated field: skip the multi-hop OR.
        }
    }
    if (!exclusiveDirect.empty()) {
        return exclusiveDirect;
    }
    return found;
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

/**
 * @brief One ShotGrid condition for a single ownership path.
 * @param node Scope leaf. `value` is an id or a parent name.
 * @param mapping Domain ↔ ShotGrid map, used to resolve a parent name field.
 * @param hops One path from `linkPathsTo`.
 * @param op ShotGrid comparison, `"is"` or `"is_not"`.
 * @return A basic condition array.
 * @throw Error When the value is neither an id nor a name, or the parent has no name field.
 */
nlohmann::json pathCondition(
    const ExprNode &node,
    const ShotGridMapping &mapping,
    const std::vector<LinkHop> &hops,
    const char *op) {
    if (const auto *id = std::get_if<std::int64_t>(&*node.value)) {
        nlohmann::json condition = nlohmann::json::array();
        condition.push_back(pathField(hops));
        condition.push_back(op);
        condition.push_back(nlohmann::json{{"type", hops.back().type_name}, {"id", *id}});
        return condition;
    }
    if (const auto *name = std::get_if<std::string>(&*node.value)) {
        const ShotGridSchema *target = mapping.findSchema(hops.back().target);
        const ShotGridSchemaField *nameField = target ? target->findField("name") : nullptr;
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

nlohmann::json scopeCondition(
    const ExprNode &node, const ShotGridMapping &mapping, const std::string &entity) {
    const char *op = comparisonOp(node.op);
    if (!op || !node.value) {
        throw Error("shotgrid: invalid scope leaf on \"" + node.key + "\"");
    }
    const std::vector<std::vector<LinkHop>> paths = linkPathsTo(mapping, entity, node.key);
    nlohmann::json conditions = nlohmann::json::array();
    for (const std::vector<LinkHop> &hops : paths) {
        conditions.push_back(pathCondition(node, mapping, hops, op));
    }
    if (conditions.size() == 1) {
        return conditions[0]; ///< One path stays a basic condition.
    }
    nlohmann::json combined;
    combined["logical_operator"] = "or"; ///< Every ownership path belongs to the same parent.
    combined["conditions"] = std::move(conditions);
    return combined;
}

nlohmann::json fieldCondition(const ExprNode &node, const ShotGridSchema &schema) {
    const char *op = comparisonOp(node.op);
    if (!op || !node.value) {
        throw Error("shotgrid: invalid field leaf on \"" + node.key + "\"");
    }
    const ShotGridSchemaField *field = schema.findField(node.key);
    if (!field || field->shotgrid.empty()) {
        throw Error("shotgrid: field \"" + schema.domainName + "." + node.key + "\" is not mapped");
    }
    return nlohmann::json::array({field->shotgrid, op, valueJson(*node.value)});
}

nlohmann::json exprToJson(
    const ExprNode &node,
    const ShotGridMapping &mapping,
    const ShotGridSchema &schema,
    bool scope) {
    if (node.kind == ExprKind::Group) {
        if (node.op == ExprOp::Not) {
            if (node.args.size() != 1 || !node.args[0]) {
                throw Error("shotgrid: not expects one child");
            }
            nlohmann::json child = exprToJson(*node.args[0], mapping, schema, scope);
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
            out["conditions"].push_back(exprToJson(*child, mapping, schema, scope));
        }
        return out;
    }
    if (scope) {
        return scopeCondition(node, mapping, schema.domainName);
    }
    return fieldCondition(node, schema);
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

/**
 * @brief Name carried by a ShotGrid entity object.
 * @param value JSON value from one attribute.
 * @return `name` when `value` is an entity object. Empty otherwise.
 */
std::string entityName(const nlohmann::json &value) {
    if (value.is_object() && value.contains("name") && value["name"].is_string()) {
        return value["name"].get<std::string>();
    }
    return {};
}

/**
 * @brief Joins the names of a ShotGrid list.
 * @param value Array of strings or entity objects.
 * @return Names separated by commas. Empty entries are skipped.
 */
std::string joinNames(const nlohmann::json &value) {
    std::string joined;
    for (const nlohmann::json &item : value) {
        const std::string name = item.is_string() ? item.get<std::string>() : entityName(item);
        if (name.empty()) {
            continue;
        }
        if (!joined.empty()) {
            joined += ',';
        }
        joined += name;
    }
    return joined;
}

/**
 * @brief Copies one ShotGrid attribute onto the domain record.
 *
 * Notes become `1` when the list is not empty. A true client flag becomes `1`.
 * Site lists keep the ShotGrid names. `shots` and `versions` become lists of links.
 * An entity object keeps its `name`.
 *
 * @param out Record being filled.
 * @param field Domain field and its ShotGrid key.
 * @param value Attribute value. Null is not passed in.
 */
void assignField(Record &out, const ShotGridSchemaField &field, const nlohmann::json &value) {
    if (field.domain == "has_notes") {
        if (value.is_array() && !value.empty()) {
            out.fields["has_notes"] = std::int64_t{1}; ///< The N button lights up.
        }
        return;
    }
    if (field.domain == "synced_to_client") {
        if (value.is_boolean() && value.get<bool>()) {
            out.fields["synced_to_client"] = std::int64_t{1}; ///< The C button lights up.
        }
        return;
    }
    if (field.domain == "uploaded_movie") {
        std::string url;
        if (value.is_string()) {
            url = value.get<std::string>();
        } else if (value.is_object() && value.contains("url") && value["url"].is_string()) {
            url = value["url"].get<std::string>(); ///< ShotGrid wraps a signed URL in an object.
        }
        if (!url.empty()) {
            out.fields["uploaded_movie"] = url;
        }
        return;
    }
    if (field.domain == "sites" && value.is_array()) {
        const std::string names = joinNames(value);
        if (!names.empty()) {
            out.fields["sites"] = names; ///< Paris, Montreal, Bangalore, as ShotGrid sent them.
        }
        return;
    }
    if ((field.domain == "shots" || field.domain == "versions") && value.is_array()) {
        if (value.empty()) {
            return; ///< An empty list leaves the field absent, same as null.
        }
        std::vector<Link> links;
        links.reserve(value.size());
        for (const nlohmann::json &item : value) {
            if (!item.is_object() || !item.contains("id") || !item["id"].is_number_integer()) {
                continue;
            }
            Link link;
            link.id = item["id"].get<std::int64_t>();
            if (item.contains("name") && item["name"].is_string()) {
                link.name = item["name"].get<std::string>(); ///< ShotGrid link name / code.
            }
            links.push_back(std::move(link));
        }
        if (!links.empty()) {
            out.fields[field.domain] = std::move(links);
        }
        return;
    }
    if (value.is_number_integer()) {
        out.fields[field.domain] = value.get<std::int64_t>();
        return;
    }
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (looksLikeDateField(field.domain)) {
            out.fields[field.domain] = pms::parseDate(text);
            return;
        }
        if (!text.empty()) {
            out.fields[field.domain] = text;
        }
        return;
    }
    const std::string name = entityName(value);
    if (!name.empty()) {
        out.fields[field.domain] = name; ///< Linked entity: keep the label the card shows.
    }
}

/**
 * @brief Picks the JSON value for one mapped ShotGrid field.
 *
 * Scalars live under `attributes`. Entity and multi-entity fields live under
 * `relationships.<field>.data` in the REST response. A value already present in
 * attributes wins, so unit fixtures that flatten links stay valid.
 *
 * @param attributes Row attributes object, or the row itself.
 * @param relationships Row relationships object, or null when absent.
 * @param shotgridField ShotGrid field name from the mapping.
 * @return Pointer to the value, or null when the field is missing or null.
 */
const nlohmann::json *findFieldValue(
    const nlohmann::json &attributes,
    const nlohmann::json *relationships,
    const std::string &shotgridField) {
    if (attributes.contains(shotgridField) && !attributes[shotgridField].is_null()) {
        return &attributes[shotgridField];
    }
    if (!relationships || !relationships->contains(shotgridField)) {
        return nullptr;
    }
    const nlohmann::json &link = (*relationships)[shotgridField];
    if (!link.is_object() || !link.contains("data") || link["data"].is_null()) {
        return nullptr; ///< REST wraps entity and multi-entity values in `data`.
    }
    return &link["data"];
}

} // namespace

nlohmann::json shotgridFiltersFromIr(const Ir &ir, const ShotGridMapping &mapping) {
    const ShotGridSchema *schema = mapping.findSchema(ir.entity);
    if (!schema || schema->shotgridName.empty()) {
        throw Error("shotgrid: entity \"" + ir.entity + "\" is not mapped");
    }
    nlohmann::json filters = nlohmann::json::object();
    if (ir.in) {
        filters = exprToJson(*ir.in, mapping, *schema, true);
    }
    if (ir.where) {
        nlohmann::json where = exprToJson(*ir.where, mapping, *schema, false);
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
        const ShotGridSchemaField *idField = schema->findField("id");
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
    // No criterion. The backend sends `filters: []`. An empty object is not a hash filter.
    if (filters.empty()) {
        return nullptr;
    }
    return filters;
}

std::vector<std::string> shotgridFieldNames(
    const ShotGridSchema &schema, const std::vector<std::string> *domainFields) {
    std::vector<std::string> names;
    names.reserve(schema.fields.size());
    for (const ShotGridSchemaField &field : schema.fields) {
        if (field.shotgrid.empty()) {
            continue;
        }
        if (domainFields) {
            const bool wanted =
                std::find(domainFields->begin(), domainFields->end(), field.domain) !=
                domainFields->end();
            if (!wanted) {
                continue; ///< Projection omits this mapped field from `?fields=`.
            }
        }
        names.push_back(field.shotgrid);
    }
    return names;
}

Record shotgridRecordFromJson(const nlohmann::json &row, const ShotGridSchema &schema) {
    const nlohmann::json *attributes = &row;
    if (row.contains("attributes") && row["attributes"].is_object()) {
        attributes = &row["attributes"];
    }
    const nlohmann::json *relationships = nullptr;
    if (row.contains("relationships") && row["relationships"].is_object()) {
        relationships = &row["relationships"];
    }
    Record out;
    for (const ShotGridSchemaField &field : schema.fields) {
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
        const nlohmann::json *value = findFieldValue(*attributes, relationships, field.shotgrid);
        if (!value) {
            continue;
        }
        assignField(out, field, *value);
    }
    for (const ShotGridSchemaField &field : schema.fields) {
        if (!field.provider.empty()) {
            out.fields[field.domain + "_provider"] = field.provider; ///< Constant from the mapping.
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
