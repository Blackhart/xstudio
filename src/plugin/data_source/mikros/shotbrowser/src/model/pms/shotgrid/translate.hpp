// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/shotgrid/translate.hpp
 * @brief IR ↔ ShotGrid filter/record helpers (no HTTP).
 */
#pragma once

#include "pms/ir.hpp"
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
 * `conditions`. Several ownership paths to one parent become an `or`. A find
 * with no criterion returns null. The HTTP body still
 * sends `filters: []`, because the site rejects a body that omits the key.
 *
 * @param ir Validated find query (op may be set).
 * @param mapping Complete domain ↔ ShotGrid map.
 * @return A hash-style filter, or null when the find has no criterion.
 * @throw Error When an entity, field, or link cannot be resolved.
 */
nlohmann::json shotgridFiltersFromIr(const Ir &ir, const ShotGridMapping &mapping);

/**
 * @brief Lists ShotGrid attribute names to request for an entity.
 * @param schema ShotGrid schema for the queried entity.
 * @return Field shotgrid names in mapping order.
 */
std::vector<std::string> shotgridFieldNames(const ShotGridSchema &schema);

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

} // namespace pms
