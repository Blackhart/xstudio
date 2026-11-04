// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/schema.hpp
 * @brief Runtime view of the domain schema loaded from YAML.
 */
#pragma once

#include <string>
#include <vector>

namespace pms {

/**
 * @brief One field of an entity, as declared in the domain YAML.
 *
 * `filter` allows the field in Where. `canSet` mirrors the YAML key `set`
 * and is loaded for later slices; this slice does not write fields.
 */
struct FieldSpec {
    std::string key;  ///< YAML field name.
    std::string type; ///< `string`, `int`, or `date`.
    bool filter = false; ///< True when Where may use the field.
    bool canSet = false; ///< True when a later slice may assign the field.
    bool requiredOnCreate = false; ///< True when a later create must send the field.
    std::vector<std::string> ops;  ///< Comparison names allowed for `type`.
};

/**
 * @brief One entity declared in the domain YAML.
 *
 * Pointers returned by field() point into this object and die with it.
 */
struct EntitySpec {
    std::string name; ///< YAML entity name, such as `"project"`.
    bool creatable = false; ///< True when a later slice may create the entity.
    std::vector<FieldSpec> fields; ///< Fields in YAML order.
    std::vector<std::string> scope; ///< Direct parents from YAML. Ancestors inherit.

    /**
     * @brief Finds a field by its YAML key.
     * @param key Field name, such as `"start_date"`.
     * @return The field, or null when the entity does not declare it.
     */
    const FieldSpec *field(const std::string &key) const;
};

/**
 * @brief Domain schema used to validate a query.
 *
 * The same YAML file feeds this loader and the proxy generator. The C++ does
 * not hard-code the field list.
 */
class Schema {
  public:
    /**
     * @brief Loads a domain schema from a YAML file.
     * @param path Path to `schema.yaml`.
     * @return The loaded schema. Entity order follows the file.
     * @throw Error When the file cannot be read or a type or entity is incomplete.
     */
    static Schema load(const std::string &path);

    /**
     * @brief Finds an entity by its YAML name.
     * @param name Entity name, such as `"project"`.
     * @return The entity, or null when the schema does not declare it.
     */
    const EntitySpec *entity(const std::string &name) const;

    /**
     * @brief Returns every entity in file order.
     * @return The entities owned by this schema.
     */
    const std::vector<EntitySpec> &entities() const { return entities_; }

    /**
     * @brief Reports whether `parent` is legal in In() for `entity`.
     *
     * Direct YAML scope parents and their ancestors all qualify. For example
     * `version` with `scope: [shot, sequence, asset, playlist]` accepts those
     * parents and `project`.
     *
     * @param entity Queried entity.
     * @param parent Parent entity name used in an In leaf.
     * @return True when the parent is in the inherited scope.
     */
    bool allowsInParent(const EntitySpec &entity, const std::string &parent) const;

  private:
    std::vector<EntitySpec> entities_; ///< Entities in YAML order.
};

} // namespace pms
