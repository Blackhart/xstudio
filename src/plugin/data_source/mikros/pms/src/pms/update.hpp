// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/update.hpp
 * @brief Writes one editable field of one record.
 */
#pragma once

#include "pms/session.hpp"
#include "pms/value.hpp"

#include <cstdint>
#include <string>

namespace pms {

/**
 * @brief Validates one assignment and asks the backend to store it.
 *
 * The field must be `editable` in the schema. When the field has labels, the
 * string value must be one of those codes. The cache in front of ShotGrid
 * drops its entries after a successful write.
 *
 * @param session Schema and backend. The backend is not owned.
 * @param entity Domain entity name, such as `"shot"`.
 * @param id Record id. Must be greater than zero.
 * @param field Domain field name, such as `"status"`.
 * @param value New value. A status code is a string.
 * @throw Error When the entity, the field, or the value is illegal, or the backend fails.
 */
void updateField(
    Session &session,
    const std::string &entity,
    std::int64_t id,
    const std::string &field,
    const Value &value);

} // namespace pms
