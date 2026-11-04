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
struct ShotGridFieldMap {
    std::string domain;   ///< Domain field key, such as `"name"`.
    std::string shotgrid; ///< REST attribute name. Empty until filled in.
};

/**
 * @brief One parent link on a ShotGrid entity.
 *
 * `shotgrid` is the field on the mapped entity. `target` is the domain parent
 * entity name used to resolve the parent's ShotGrid type and name field.
 */
struct ShotGridLinkMap {
    std::string domain;   ///< Domain parent key, such as `"project"`.
    std::string shotgrid; ///< REST link field on the child entity.
    std::string target;   ///< Domain entity name of the parent.
};

/**
 * @brief ShotGrid names for one domain entity.
 */
struct ShotGridEntityMap {
    std::string domain;   ///< Domain entity name, such as `"version"`.
    std::string shotgrid; ///< REST entity type, such as `"Version"`.
    std::vector<ShotGridFieldMap> fields; ///< Domain fields in YAML order.
    std::vector<ShotGridLinkMap> links;   ///< Domain parents in YAML order.

    /**
     * @brief Finds a field mapping by domain key.
     * @param key Domain field name.
     * @return The mapping, or null when absent.
     */
    const ShotGridFieldMap *field(const std::string &key) const;

    /**
     * @brief Finds a link mapping by domain parent key.
     * @param key Domain parent name.
     * @return The mapping, or null when absent.
     */
    const ShotGridLinkMap *link(const std::string &key) const;
};

/**
 * @brief Full ShotGrid mapping for the current domain slice.
 *
 * Empty `shotgrid` strings are allowed at load time so the file can be filled
 * gradually. Call requireComplete() before find() against a live site.
 */
class ShotGridMapping {
  public:
    /**
     * @brief Loads a mapping YAML file.
     * @param path Path to `shotgrid_mapping.yaml`.
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
     * @brief Finds an entity mapping by domain name.
     * @param name Domain entity name.
     * @return The mapping, or null when absent.
     */
    const ShotGridEntityMap *entity(const std::string &name) const;

    /**
     * @brief Returns every entity mapping in file order.
     * @return The entities owned by this mapping.
     */
    const std::vector<ShotGridEntityMap> &entities() const { return entities_; }

    /**
     * @brief Throws when any entity, field, or link still has an empty shotgrid name.
     * @throw Error Naming the first incomplete entry.
     */
    void requireComplete() const;

  private:
    std::vector<ShotGridEntityMap> entities_; ///< Entities in YAML order.
};

} // namespace pms
