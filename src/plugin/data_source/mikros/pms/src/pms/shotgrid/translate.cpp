// SPDX-License-Identifier: Apache-2.0
/**
 * @file shotgrid/translate.cpp
 * @brief Builds ShotGrid filters and maps response rows to domain records.
 */

#include "pms/shotgrid/translate.hpp"

#include "pms/error.hpp"

#include <algorithm>
#include <iomanip>
#include <optional>
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
        return "greater_than";
    case ExprOp::Lt:
        return "less_than";
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
    if (const auto *texts = std::get_if<std::vector<std::string>>(&value)) {
        return nlohmann::json(*texts); ///< ShotGrid `in` wants the array itself.
    }
    if (const auto *numbers = std::get_if<std::vector<std::int64_t>>(&value)) {
        return nlohmann::json(*numbers);
    }
    if (const auto *period = std::get_if<Period>(&value)) {
        return nlohmann::json::array({period->count, period->unit}); ///< `[7, "DAY"]`.
    }
    if (const auto *bounds = std::get_if<IntBounds>(&value)) {
        return nlohmann::json::array({bounds->low, bounds->high}); ///< REST `between` pair.
    }
    if (const auto *flag = std::get_if<bool>(&value)) {
        return *flag; ///< A checkbox filter is a JSON boolean.
    }
    if (const auto *number = std::get_if<double>(&value)) {
        return *number;
    }
    if (const auto *doubles = std::get_if<std::vector<double>>(&value)) {
        return nlohmann::json(*doubles);
    }
    if (const auto *bounds = std::get_if<FloatBounds>(&value)) {
        return nlohmann::json::array({bounds->low, bounds->high});
    }
    if (const auto *link = std::get_if<Link>(&value)) {
        return nlohmann::json{{"type", link->type}, {"id", link->id}};
    }
    if (const auto *links = std::get_if<std::vector<Link>>(&value)) {
        nlohmann::json array = nlohmann::json::array();
        for (const Link &item : *links) {
            array.push_back({{"type", item.type}, {"id", item.id}});
        }
        return array;
    }
    if (const auto *bounds = std::get_if<DateBounds>(&value)) {
        auto formatDay = [](const Date &date) {
            std::ostringstream out;
            out << std::setfill('0') << std::setw(4) << date.year << '-' << std::setw(2)
                << date.month << '-' << std::setw(2) << date.day;
            return out.str();
        };
        return nlohmann::json::array({formatDay(bounds->low), formatDay(bounds->high)});
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
 * @brief Maps a domain name operator to the text operator used on `cached_display_name`.
 * @param op Domain operator.
 * @return `contains` or `not_contains`. Null when `op` stays a name operator.
 *     `name_is` is sent as itself, and only an entity field accepts it.
 */
const char *displayNameOp(ExprOp op) {
    switch (op) {
    case ExprOp::NameContains:
        return "contains";
    case ExprOp::NameNotContains:
        return "not_contains";
    default:
        return nullptr;
    }
}

/**
 * @brief Builds one condition, or an `or`, that filters a multi-entity list by label.
 * @param path ShotGrid path of the multi_entity field.
 * @param textOp `contains`, `starts_with`, or `is`.
 * @param value Filter text.
 * @param validTypes Linked types from the ShotGrid field schema.
 * @return One basic condition when a single type is legal, otherwise an `or`.
 * @throw Error When `validTypes` is empty or a type name is empty.
 */
nlohmann::json multiEntityNameCondition(
    const std::string &path,
    const char *textOp,
    const nlohmann::json &value,
    const std::vector<std::string> &validTypes) {
    if (validTypes.empty()) {
        throw Error("shotgrid: multi_entity path \"" + path + "\" has no valid_types");
    }
    nlohmann::json conditions = nlohmann::json::array();
    for (const std::string &typeName : validTypes) {
        if (typeName.empty()) {
            throw Error("shotgrid: multi_entity path \"" + path + "\" has an empty valid type");
        }
        conditions.push_back(nlohmann::json::array(
            {path + "." + typeName + ".cached_display_name", textOp, value}));
    }
    if (conditions.size() == 1) {
        return conditions[0]; ///< One linked type stays a basic condition.
    }
    nlohmann::json combined;
    combined["logical_operator"] = "or"; ///< Every valid type can carry the same label.
    combined["conditions"] = std::move(conditions);
    return combined;
}

/**
 * @brief Builds the ShotGrid condition for one mapped field path.
 *
 * A `name_*` leaf asks `resolve` about the path's leaf. `multi_entity` is
 * rewritten onto `cached_display_name`. `entity` keeps the `name_*` operator.
 * Any other operator is copied through.
 *
 * @param exprOp Domain operator.
 * @param shotgridOp ShotGrid operator from `comparisonOp`.
 * @param path Mapped ShotGrid path, possibly dotted.
 * @param value Filter value already converted to JSON.
 * @param fallbackOwner ShotGrid type used when `path` has no dot.
 * @param resolve Field schema. Empty leaves `name_*` unchanged.
 * @return A basic condition, or an `or` of display-name conditions.
 * @throw Error When a multi_entity field has no `valid_types`.
 */
nlohmann::json conditionOnPath(
    ExprOp exprOp,
    const char *shotgridOp,
    const std::string &path,
    const nlohmann::json &value,
    const std::string &fallbackOwner,
    const FieldSchemaResolve &resolve) {
    const char *textOp = displayNameOp(exprOp);
    if (!textOp || !resolve) {
        return nlohmann::json::array({path, shotgridOp, value}); ///< Not a name filter, or no schema.
    }
    const ShotGridFieldPathLeaf leaf = splitShotGridFieldPath(path, fallbackOwner);
    const ShotGridFieldInfo info = resolve(leaf.owner, leaf.field);
    if (info.data_type != "multi_entity") {
        return nlohmann::json::array({path, shotgridOp, value}); ///< entity keeps the name_* operator.
    }
    return multiEntityNameCondition(path, textOp, value, info.valid_types);
}

/**
 * @brief One ShotGrid condition for a parent field along one ownership path.
 * @param node In field leaf (`parent` + `key` + value).
 * @param mapping Domain ↔ ShotGrid map for the parent field mapping.
 * @param hops One path from `linkPathsTo` (link fields from mapping.links).
 * @param op ShotGrid comparison (`is`, `is_not`, `contains`, …).
 * @param resolve Field schema used when the leaf is `name_*`.
 * @return A basic condition array, or an `or` when the leaf is multi_entity.
 * @throw Error When the parent field is unmapped or the value is missing.
 */
nlohmann::json pathCondition(
    const ExprNode &node,
    const ShotGridMapping &mapping,
    const std::vector<LinkHop> &hops,
    const char *op,
    const FieldSchemaResolve &resolve) {
    if (!node.value) {
        throw Error("shotgrid: parent field \"" + node.parent + "." + node.key + "\" has no value");
    }
    // Parent id equality uses the link field itself: ["entity", "is", {type, id}].
    if (node.key == "id" && (node.op == ExprOp::Eq || node.op == ExprOp::Ne)) {
        const auto *id = std::get_if<std::int64_t>(&*node.value);
        if (!id) {
            throw Error("shotgrid: parent id expects an int");
        }
        nlohmann::json condition = nlohmann::json::array();
        condition.push_back(pathField(hops));
        condition.push_back(op);
        condition.push_back(nlohmann::json{{"type", hops.back().type_name}, {"id", *id}});
        return condition;
    }
    const ShotGridSchema *target = mapping.findSchema(hops.back().target);
    const ShotGridSchemaField *field = target ? target->findField(node.key) : nullptr;
    if (!field || field->shotgrid.empty()) {
        throw Error(
            "shotgrid: parent \"" + node.parent + "\" field \"" + node.key + "\" is not mapped");
    }
    return conditionOnPath(
        node.op,
        op,
        pathField(hops, field->shotgrid),
        valueJson(*node.value),
        hops.back().type_name,
        resolve);
}

/**
 * @brief Builds ShotGrid filter conditions for one In() field leaf.
 * @param node Field leaf stamped with domain `parent`.
 * @param mapping Domain ↔ ShotGrid map.
 * @param entity Queried domain entity.
 * @param resolve Field schema used when the leaf is `name_*`.
 * @return One condition, or an `or` of paths when several ownership links exist.
 */
nlohmann::json parentFieldCondition(
    const ExprNode &node,
    const ShotGridMapping &mapping,
    const std::string &entity,
    const FieldSchemaResolve &resolve) {
    const char *op = comparisonOp(node.op);
    if (!op || node.parent.empty()) {
        throw Error("shotgrid: invalid In leaf on \"" + node.key + "\"");
    }
    const std::vector<std::vector<LinkHop>> paths = linkPathsTo(mapping, entity, node.parent);
    nlohmann::json conditions = nlohmann::json::array();
    for (const std::vector<LinkHop> &hops : paths) {
        conditions.push_back(pathCondition(node, mapping, hops, op, resolve));
    }
    if (conditions.size() == 1) {
        return conditions[0]; ///< One path stays a basic condition.
    }
    nlohmann::json combined;
    combined["logical_operator"] = "or"; ///< Every ownership path belongs to the same parent.
    combined["conditions"] = std::move(conditions);
    return combined;
}

/**
 * @brief Builds the ShotGrid condition for one where-field leaf.
 * @param node Field leaf on the queried entity.
 * @param schema ShotGrid schema for the queried entity.
 * @param resolve Field schema used when the leaf is `name_*`.
 * @return A basic condition, or an `or` when the leaf is multi_entity.
 * @throw Error When the field is unmapped or the value is missing.
 */
nlohmann::json fieldCondition(
    const ExprNode &node, const ShotGridSchema &schema, const FieldSchemaResolve &resolve) {
    const char *op = comparisonOp(node.op);
    if (!op || !node.value) {
        throw Error("shotgrid: invalid field leaf on \"" + node.key + "\"");
    }
    const ShotGridSchemaField *field = schema.findField(node.key);
    if (!field || field->shotgrid.empty()) {
        throw Error("shotgrid: field \"" + schema.domainName + "." + node.key + "\" is not mapped");
    }
    return conditionOnPath(
        node.op, op, field->shotgrid, valueJson(*node.value), schema.shotgridName, resolve);
}

nlohmann::json exprToJson(
    const ExprNode &node,
    const ShotGridMapping &mapping,
    const ShotGridSchema &schema,
    bool isIn,
    const FieldSchemaResolve &resolve) {
    if (node.kind == ExprKind::Group) {
        if (node.op == ExprOp::Not) {
            if (node.args.size() != 1 || !node.args[0]) {
                throw Error("shotgrid: not expects one child");
            }
            nlohmann::json child = exprToJson(*node.args[0], mapping, schema, isIn, resolve);
            if (!child.is_array() || child.size() < 2) {
                throw Error("shotgrid: not only inverts a leaf");
            }
            const std::string op = child[1].get<std::string>();
            if (op == "is") {
                child[1] = "is_not";
            } else if (op == "is_not") {
                child[1] = "is";
            } else {
                throw Error("shotgrid: not only inverts is and is_not");
            }
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
            out["conditions"].push_back(exprToJson(*child, mapping, schema, isIn, resolve));
        }
        return out;
    }
    if (node.kind != ExprKind::Field) {
        throw Error("shotgrid: invalid expression leaf");
    }
    if (isIn) {
        return parentFieldCondition(node, mapping, schema.domainName, resolve);
    }
    return fieldCondition(node, schema, resolve);
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
    if (!value.is_object()) {
        return {};
    }
    if (value.contains("name") && value["name"].is_string()) {
        return value["name"].get<std::string>();
    }
    if (value.contains("code") && value["code"].is_string()) {
        return value["code"].get<std::string>(); ///< Shot / Asset links often use code.
    }
    return {};
}

/**
 * @brief Reads one ShotGrid entity, or a bare name, as a link.
 * @param item Entity object, or a string name from a list.
 * @return The link. Empty when the item has neither an id nor a name.
 */
std::optional<Link> linkFromJson(const nlohmann::json &item) {
    Link link;
    if (item.is_string()) {
        link.name = item.get<std::string>(); ///< A list entry that is only a label.
    } else if (item.is_object()) {
        if (item.contains("id") && item["id"].is_number_integer()) {
            link.id = item["id"].get<std::int64_t>();
        }
        link.name = entityName(item);
        if (item.contains("type") && item["type"].is_string()) {
            link.type = item["type"].get<std::string>(); ///< Shot, Playlist, HumanUser, …
        }
        if (item.contains("image") && item["image"].is_string()) {
            link.image = item["image"].get<std::string>(); ///< Attachment thumbnail URL.
        } else if (item.contains("url") && item["url"].is_string()) {
            link.image = item["url"].get<std::string>();
        }
    } else {
        return std::nullopt;
    }
    if (link.id == 0 && link.name.empty()) {
        return std::nullopt; ///< Nothing the screen can show or open.
    }
    return link;
}

/**
 * @brief Copies one ShotGrid attribute onto the domain record.
 *
 * A checkbox becomes a bool. A float becomes a double. An entity object
 * becomes one `Link`. Every array becomes a list of `Link`, including a bare
 * name in that array. An empty array leaves the field absent. The DAL joins
 * names when a screen wants one label.
 *
 * @param out Record being filled.
 * @param field Domain field and its ShotGrid key.
 * @param value Attribute value. Null is not passed in.
 */
void assignField(Record &out, const ShotGridSchemaField &field, const nlohmann::json &value) {
    if (field.type == "checkbox") {
        if (value.is_boolean()) {
            out.fields[field.domain] = value.get<bool>();
        }
        return;
    }
    if (field.type == "float" && value.is_number()) {
        out.fields[field.domain] = value.get<double>();
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
    if (value.is_array()) {
        std::vector<Link> links;
        links.reserve(value.size());
        for (const nlohmann::json &item : value) {
            const std::optional<Link> link = linkFromJson(item);
            if (!link) {
                continue;
            }
            links.push_back(*link);
        }
        if (!links.empty()) {
            out.fields[field.domain] = std::move(links); ///< Id, name, and type stay together.
        }
        return; ///< An empty list leaves the field absent, same as null.
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
    const std::optional<Link> link = linkFromJson(value);
    if (link) {
        out.fields[field.domain] = *link; ///< One entity stays one object.
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

nlohmann::json shotgridFiltersFromIr(
    const Ir &ir, const ShotGridMapping &mapping, const FieldSchemaResolve &fieldSchema) {
    const ShotGridSchema *schema = mapping.findSchema(ir.entity);
    if (!schema || schema->shotgridName.empty()) {
        throw Error("shotgrid: entity \"" + ir.entity + "\" is not mapped");
    }
    nlohmann::json filters = nlohmann::json::object();
    if (ir.in) {
        filters = exprToJson(*ir.in, mapping, *schema, true, fieldSchema);
    }
    if (ir.where) {
        nlohmann::json where = exprToJson(*ir.where, mapping, *schema, false, fieldSchema);
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
