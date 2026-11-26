// SPDX-License-Identifier: Apache-2.0
/**
 * @file shotgrid/field_schema.cpp
 * @brief Parses ShotGrid field schemas and caches them by entity and field.
 */

#include "pms/shotgrid/field_schema.hpp"

#include "pms/error.hpp"

#include <cctype>
#include <utility>

namespace pms {
namespace {

/**
 * @brief Splits a dotted path, rejecting empty segments.
 * @param path Mapping path.
 * @return Segments in order.
 * @throw Error When a segment is empty.
 */
std::vector<std::string> splitDots(const std::string &path) {
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : path) {
        if (ch != '.') {
            current.push_back(ch);
            continue;
        }
        if (current.empty()) {
            throw Error("shotgrid: empty segment in field path \"" + path + "\"");
        }
        parts.push_back(current); ///< One hop or type name.
        current.clear();
    }
    if (current.empty()) {
        throw Error("shotgrid: empty segment in field path \"" + path + "\"");
    }
    parts.push_back(current);
    return parts;
}

/**
 * @brief Reads a REST property that is either a string or `{ "value": string }`.
 * @param node JSON property.
 * @param what Name used in the error.
 * @return The string.
 * @throw Error When the shape is neither.
 */
std::string readWrappedString(const nlohmann::json &node, const std::string &what) {
    if (node.is_string()) {
        return node.get<std::string>();
    }
    if (node.is_object() && node.contains("value") && node["value"].is_string()) {
        return node["value"].get<std::string>(); ///< Schema properties wrap the payload in `value`.
    }
    throw Error("shotgrid: schema " + what + " is not a string");
}

/**
 * @brief Reads `valid_types` from a field's properties object.
 * @param properties `properties` from the schema body. May be missing the key.
 * @return Linked type names, in response order. Empty when the key is absent or null.
 * @throw Error When the value is present and is not a list of strings.
 */
std::vector<std::string> readValidTypes(const nlohmann::json &properties) {
    if (!properties.is_object() || !properties.contains("valid_types") ||
        properties["valid_types"].is_null()) {
        return {}; ///< A text field has no linked types.
    }
    const nlohmann::json *list = &properties["valid_types"];
    if (list->is_object()) {
        if (!list->contains("value") || (*list)["value"].is_null()) {
            return {};
        }
        list = &(*list)["value"]; ///< `{ "value": ["Shot"], "editable": false }`.
    }
    if (!list->is_array()) {
        throw Error("shotgrid: schema valid_types is not a list");
    }
    std::vector<std::string> types;
    types.reserve(list->size());
    for (const nlohmann::json &item : *list) {
        types.push_back(readWrappedString(item, "valid_types entry"));
    }
    return types;
}

} // namespace

ShotGridFieldPathLeaf splitShotGridFieldPath(const std::string &path, const std::string &queriedType) {
    const std::vector<std::string> parts = splitDots(path);
    if (parts.size() == 1) {
        if (queriedType.empty()) {
            throw Error("shotgrid: field \"" + path + "\" has no owner type");
        }
        return ShotGridFieldPathLeaf{queriedType, parts[0]}; ///< Direct field on the queried entity.
    }
    if (parts.size() % 2 == 0) {
        throw Error("shotgrid: field path \"" + path + "\" has no leaf type");
    }
    return ShotGridFieldPathLeaf{parts[parts.size() - 2], parts.back()};
}

std::string shotGridSchemaEntityName(const std::string &typeName) {
    if (typeName.empty()) {
        throw Error("shotgrid: schema entity type is empty");
    }
    std::string out;
    out.reserve(typeName.size() + 4);
    for (std::size_t i = 0; i < typeName.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(typeName[i]);
        if (i > 0) {
            const unsigned char prev = static_cast<unsigned char>(typeName[i - 1]);
            const bool upper = std::isupper(ch) != 0;
            const bool digit = std::isdigit(ch) != 0;
            const bool prevLowerOrDigit = std::islower(prev) != 0 || std::isdigit(prev) != 0;
            const bool nextLower = i + 1 < typeName.size() &&
                                   std::islower(static_cast<unsigned char>(typeName[i + 1])) != 0;
            if (upper && (prevLowerOrDigit || nextLower)) {
                out.push_back('_'); ///< `PublishedFile` → `published_file`.
            } else if (digit && std::isalpha(prev) != 0) {
                out.push_back('_'); ///< `Entity01` → `entity_01`.
            }
        }
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

ShotGridFieldInfo shotGridFieldInfoFromJson(const nlohmann::json &body) {
    if (!body.is_object() || !body.contains("data") || !body["data"].is_object()) {
        throw Error("shotgrid: schema field response missing data");
    }
    const nlohmann::json &data = body["data"];
    if (!data.contains("data_type")) {
        throw Error("shotgrid: schema field response missing data_type");
    }
    ShotGridFieldInfo info;
    info.data_type = readWrappedString(data["data_type"], "data_type");
    const nlohmann::json properties =
        data.contains("properties") ? data["properties"] : nlohmann::json::object();
    info.valid_types = readValidTypes(properties);
    return info;
}

const ShotGridFieldInfo *ShotGridFieldSchemaCache::find(
    const std::string &ownerType, const std::string &field) const {
    const auto owner = entries_.find(ownerType);
    if (owner == entries_.end()) {
        return nullptr;
    }
    const auto found = owner->second.find(field);
    if (found == owner->second.end()) {
        return nullptr;
    }
    return &found->second;
}

void ShotGridFieldSchemaCache::store(
    const std::string &ownerType, const std::string &field, ShotGridFieldInfo info) {
    entries_[ownerType][field] = std::move(info);
}

ShotGridFieldInfo cachedFieldInfo(
    ShotGridFieldSchemaCache &cache,
    const std::string &ownerType,
    const std::string &field,
    const std::function<nlohmann::json(const std::string &ownerType, const std::string &field)>
        &fetch) {
    if (const ShotGridFieldInfo *cached = cache.find(ownerType, field)) {
        return *cached; ///< Copy before any later insert can rehash the map.
    }
    const nlohmann::json body = fetch(ownerType, field);
    ShotGridFieldInfo info = shotGridFieldInfoFromJson(body);
    cache.store(ownerType, field, info);
    return info;
}

} // namespace pms
