// SPDX-License-Identifier: Apache-2.0
/**
 * @file mapped_schema.hpp
 * @brief Test helper that copies ShotGrid types onto the domain schema.
 */
#pragma once

#include "pms/schema.hpp"
#include "pms/shotgrid/mapping.hpp"

/**
 * @brief Loads the domain schema and the example mapping types.
 * @return Schema whose fields carry ShotGrid types and operators.
 */
inline pms::Schema mappedSchema() {
    pms::Schema schema = pms::Schema::load(PMS_SCHEMA_PATH);
    schema.applyMapping(pms::ShotGridMapping::load(PMS_SHOTGRID_MAPPING_EXAMPLE_PATH));
    return schema;
}
