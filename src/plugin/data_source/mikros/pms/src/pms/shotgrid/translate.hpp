// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/shotgrid/translate.hpp
 * @brief IR ↔ ShotGrid filter/record helpers (no HTTP).
 */
#pragma once

#include "pms/ir.hpp"
#include "pms/shotgrid/field_schema.hpp"
#include "pms/shotgrid/mapping.hpp"
#include "pms/value.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace pms {

/**
 * @brief Builds the ShotGrid `_search` filters value for a find IR.
 *
 * Hash style, as documented at
 * https://developers.shotgridsoftware.com/rest-api/#searching : a basic
 * condition array, or a complex filter with `logical_operator` and
 * `conditions`. Several ownership paths to one parent become an `or`, except
 * when a direct exclusive link exists (version `project`). A find with no
 * criterion returns null. The HTTP body still sends `filters: []`, because the
 * site rejects a body that omits the key.
 *
 * When `fieldSchema` is set, a `name_*` leaf whose ShotGrid `data_type` is
 * `multi_entity` is rewritten to `path.<valid_type>.cached_display_name` with
 * `contains`, `starts_with`, or `is`. Several `valid_types` become an `or`.
 * An `entity` leaf keeps `name_*`. An empty `fieldSchema` leaves `name_*`
 * unchanged.
 *
 * @param ir Validated find query (op may be set).
 * @param mapping Complete domain ↔ ShotGrid map.
 * @param fieldSchema ShotGrid field schema. Empty in tests that do not rewrite.
 * @return A hash-style filter, or null when the find has no criterion.
 * @throw Error When an entity, field, or link cannot be resolved, or a
 *     multi_entity field has no `valid_types`.
 */
nlohmann::json shotgridFiltersFromIr(
    const Ir &ir,
    const ShotGridMapping &mapping,
    const FieldSchemaResolve &fieldSchema = {});

/**
 * @brief Lists ShotGrid attribute names to request for an entity.
 * @param schema ShotGrid schema for the queried entity.
 * @param domainFields Domain keys to keep, or null for every mapped field.
 * @return Field shotgrid names in mapping order.
 */
std::vector<std::string> shotgridFieldNames(
    const ShotGridSchema &schema,
    const std::vector<std::string> *domainFields = nullptr);

/**
 * @brief Maps one ShotGrid data row into a domain Record.
 *
 * Scalar fields are read from `attributes`. Entity and multi-entity fields are
 * read from `relationships.<field>.data`, which is where the REST `_search`
 * response stores them.
 *
 * @param row One element of the `_search` `data` array.
 * @param schema ShotGrid schema for the queried entity.
 * @return Domain record with only mapped fields.
 * @throw Error When a required attribute is missing or mistyped.
 */
Record shotgridRecordFromJson(const nlohmann::json &row, const ShotGridSchema &schema);

/**
 * @brief Builds the JSON body of one field update.
 *
 * The key is the mapped ShotGrid attribute. A dotted name, such as
 * `sg_task.Task.step`, is rejected: it is a read path, not a column.
 *
 * @param ir Validated update. `value` is a string.
 * @param mapping Domain ↔ ShotGrid map.
 * @return Object with one attribute, such as `{"sg_status_list":"ip"}`.
 * @throw Error When the field is missing or its ShotGrid name is a path.
 */
nlohmann::json shotgridUpdateBody(const Ir &ir, const ShotGridMapping &mapping);

} // namespace pms
