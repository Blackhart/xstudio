// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/shotgrid/field_schema.hpp
 * @brief ShotGrid field schema: data_type, valid_types, and a process cache.
 */
#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pms {

/**
 * @brief Schema of one ShotGrid field, enough to rewrite a name filter.
 */
struct ShotGridFieldInfo {
    std::string data_type; ///< ShotGrid `data_type`, such as `"multi_entity"` or `"entity"`.
    std::vector<std::string> valid_types; ///< Linked entity types. Empty when the field lists none.
};

/**
 * @brief Loads `data_type` and `valid_types` for one field on one entity type.
 *
 * `ownerType` is the ShotGrid type that owns the leaf (`PublishedFile`,
 * `Playlist`). `field` is the leaf name (`sg_sites`), never a dotted path.
 */
using FieldSchemaResolve =
    std::function<ShotGridFieldInfo(const std::string &ownerType, const std::string &field)>;

/**
 * @brief Owner type and leaf name of a dotted ShotGrid field path.
 */
struct ShotGridFieldPathLeaf {
    std::string owner; ///< Entity type that owns the leaf. The queried type when the path has no dot.
    std::string field; ///< Leaf field name.
};

/**
 * @brief Reads the leaf of a ShotGrid field path.
 *
 * `sg_sites` stays on `queriedType`. `a.Type.b` and longer `field.Type.field`
 * chains yield the last type and the last field. The caller then asks the
 * schema endpoint for that pair.
 *
 * @param path Mapping path, such as `sg_published_file.PublishedFile.sg_sites`.
 * @param queriedType ShotGrid type of the entity being searched. Used when `path` has no dot.
 * @return Owner type and leaf field.
 * @throw Error When a segment is empty or a dotted path has no type before the leaf.
 */
ShotGridFieldPathLeaf splitShotGridFieldPath(const std::string &path, const std::string &queriedType);

/**
 * @brief Converts a ShotGrid entity type to the schema URL segment.
 *
 * `GET /api/v1.1/schema/{entity}/fields/{field}` wants snake_case, not the
 * plural collection used by `/entity/`. `PublishedFile` becomes
 * `published_file`. `CustomNonProjectEntity01` becomes
 * `custom_non_project_entity_01`.
 *
 * @param typeName ShotGrid entity type from the mapping or from `valid_types`.
 * @return Path segment for `/schema/{entity}`.
 * @throw Error When `typeName` is empty.
 */
std::string shotGridSchemaEntityName(const std::string &typeName);

/**
 * @brief Reads `data_type` and `valid_types` from a schema-field response.
 *
 * The REST body wraps each property as `{ "value": ... }`. `valid_types` may
 * be absent on a field that has no linked types.
 *
 * @param body JSON body of `GET /schema/{entity}/fields/{field}`.
 * @return The field info.
 * @throw Error When `data` or `data_type` is missing or mistyped.
 */
ShotGridFieldInfo shotGridFieldInfoFromJson(const nlohmann::json &body);

/**
 * @brief Remembers ShotGrid field schemas for the life of one backend.
 *
 * The key is the entity type plus the leaf field name. A miss is filled by
 * `cachedFieldInfo`.
 */
class ShotGridFieldSchemaCache {
  public:
    /**
     * @brief Returns a stored schema.
     * @param ownerType Entity type that owns the field.
     * @param field Leaf field name.
     * @return The stored info, or null when this pair has not been fetched.
     */
    const ShotGridFieldInfo *find(const std::string &ownerType, const std::string &field) const;

    /**
     * @brief Stores one schema, replacing a previous entry for the same pair.
     * @param ownerType Entity type that owns the field.
     * @param field Leaf field name.
     * @param info Schema returned by the REST endpoint.
     */
    void store(const std::string &ownerType, const std::string &field, ShotGridFieldInfo info);

  private:
    std::unordered_map<std::string, std::unordered_map<std::string, ShotGridFieldInfo>>
        entries_; ///< Outer key is the entity type. Inner key is the field name.
};

/**
 * @brief Returns the cached schema, fetching and storing it on a miss.
 * @param cache Cache owned by the backend.
 * @param ownerType Entity type that owns the field.
 * @param field Leaf field name.
 * @param fetch Loads the raw JSON body. Called only when the pair is absent.
 * @return The field info.
 * @throw Error When `fetch` or the JSON parse fails.
 */
ShotGridFieldInfo cachedFieldInfo(
    ShotGridFieldSchemaCache &cache,
    const std::string &ownerType,
    const std::string &field,
    const std::function<nlohmann::json(const std::string &ownerType, const std::string &field)>
        &fetch);

} // namespace pms
