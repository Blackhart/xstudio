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
 * @brief Builds the ShotGrid `_search` filters object for a find IR.
 * @param ir Validated find query (op may be set).
 * @param mapping Complete domain ↔ ShotGrid map.
 * @return JSON with `logical_operator` / `conditions`, or empty object when
 *     neither In nor Where is present.
 * @throw Error When an entity, field, or link cannot be resolved.
 */
nlohmann::json shotgridFiltersFromIr(const Ir &ir, const ShotGridMapping &mapping);

/**
 * @brief Lists ShotGrid attribute names to request for an entity.
 * @param entity Mapped entity.
 * @return Field shotgrid names in mapping order.
 */
std::vector<std::string> shotgridFieldNames(const ShotGridEntityMap &entity);

/**
 * @brief Maps one ShotGrid data row into a domain Record.
 * @param row One element of the `_search` `data` array.
 * @param entity Mapping for the queried domain entity.
 * @return Domain record with only mapped fields.
 * @throw Error When a required attribute is missing or mistyped.
 */
Record shotgridRecordFromJson(const nlohmann::json &row, const ShotGridEntityMap &entity);

} // namespace pms
