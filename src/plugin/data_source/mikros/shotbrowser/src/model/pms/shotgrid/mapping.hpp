// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/shotgrid/mapping.hpp
 * @brief Domain ↔ ShotGrid name map loaded from YAML.
 */
#pragma once

#include <string>
#include <vector>

namespace pms {

/**
 * @brief One domain field mapped to a ShotGrid attribute name.
 */
struct ShotGridSchemaField {
    std::string domain;   ///< Domain field key, such as `"name"`.
    std::string shotgrid; ///< REST attribute name. Empty until filled in.
    std::string provider; ///< How a worker reads the value. Empty when the field is plain text.
};

/**
 * @brief One parent link on a ShotGrid entity.
 *
 * `shotgrid` is the field on the mapped entity. `shotgridTargetName` is the
 * domain parent name used to resolve the parent's ShotGrid type and name field.
 */
struct ShotGridSchemaLink {
    std::string domain;   ///< Domain parent key, such as `"project"`.
    std::string shotgrid; ///< REST link field on the child entity.
    std::string shotgridTargetName; ///< Domain entity name of the parent.
};

/**
 * @brief ShotGrid schema for one domain entity: type, fields, and parent links.
 */
struct ShotGridSchema {
    std::string domainName;   ///< Domain entity name, such as `"version"`.
    std::string shotgridName; ///< REST entity type, such as `"Version"`.
    std::vector<ShotGridSchemaField> fields; ///< Domain fields in YAML order.
    std::vector<ShotGridSchemaLink> links;   ///< Domain parents in YAML order.

    /**
     * @brief Finds a field mapping by domain key.
     * @param key Domain field name.
     * @return The mapping, or null when absent.
     */
    const ShotGridSchemaField *findField(const std::string &key) const;

    /**
     * @brief Finds a link mapping by domain parent key.
     * @param key Domain parent name.
     * @return The mapping, or null when absent.
     */
    const ShotGridSchemaLink *findLink(const std::string &key) const;
};

/**
 * @brief Full ShotGrid mapping for the current domain slice.
 *
 * `load` and `parse` accept an empty ShotGrid name so a file can be filled
 * gradually. `throwIfNamesMissing` rejects that file before a live search.
 */
class ShotGridMapping {
  public:
    /**
     * @brief Loads a mapping YAML file.
     * @param path Path to a ShotGrid mapping YAML file.
     * @return The loaded mapping.
     * @throw Error When the file cannot be read or an entry is malformed.
     */
    static ShotGridMapping load(const std::string &path);

    /**
     * @brief Loads a mapping from YAML text (tests / fixtures).
     * @param yaml Mapping document as a string.
     * @return The loaded mapping.
     * @throw Error When the text is malformed.
     */
    static ShotGridMapping parse(const std::string &yaml);

    /**
     * @brief Finds the schema whose domain name matches.
     * @param domainName Domain entity name, such as `"project"`.
     * @return The schema, or null when absent.
     */
    const ShotGridSchema *findSchema(const std::string &domainName) const;

    /**
     * @brief Returns every entity mapping in file order.
     * @return The entities owned by this mapping.
     */
    const std::vector<ShotGridSchema> &entities() const { return entities_; }

    /**
     * @brief Throws when a ShotGrid name is empty or a link target is unknown.
     * @throw Error Naming the first missing name.
     */
    void throwIfNamesMissing() const;

  private:
    std::vector<ShotGridSchema> entities_; ///< Entities in YAML order.
};

} // namespace pms
