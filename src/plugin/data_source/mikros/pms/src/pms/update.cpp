// SPDX-License-Identifier: Apache-2.0
/**
 * @file update.cpp
 * @brief Builds an Update IR and runs it.
 */

#include "pms/update.hpp"

#include "pms/backend.hpp"
#include "pms/validate.hpp"

namespace pms {

void updateField(
    Session &session,
    const std::string &entity,
    std::int64_t id,
    const std::string &field,
    const Value &value) {
    Ir ir;
    ir.op = Op::Update;
    ir.entity = entity;
    ir.id = id;
    ir.field = field;
    ir.value = value;
    validate(ir, session.schema()); ///< Rejects a field that is not editable.
    session.backend().update(ir);
}

} // namespace pms
