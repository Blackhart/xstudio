// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/validate.hpp
 * @brief Checks a query and a returned row against the domain schema.
 */
#pragma once

#include "pms/ir.hpp"
#include "pms/schema.hpp"

namespace pms {

/**
 * @brief Checks that a find or count query matches the schema.
 *
 * The entity must exist. Each where-leaf must name a filterable field, use an
 * allowed operator, and carry a value that operator accepts. `and` and `or` need
 * two children, `not` needs one. A null where-tree is legal and lists every row.
 * For Find, when `limit` is set it must be greater than zero, and a cursor
 * without a limit is rejected. For Count, `limit` and `cursor` are rejected.
 * Query::ir() does not call this.
 *
 * @param ir Query to check. `op` must be Find or Count.
 * @param schema Domain schema loaded for the session.
 * @throw Error When the verb, the entity, a where-node, or pagination is illegal.
 */
void validate(const Ir &ir, const Schema &schema);

/**
 * @brief Checks the identity of a row returned by the backend.
 *
 * `id` must be an int and `name` must be a string. Other keys are left to the
 * caller. An empty find() result skips this check.
 *
 * @param record Row to check, including no nested entities in this slice.
 * @throw Error When `id` or `name` is missing or has the wrong type.
 */
void checkRecord(const Record &record);

} // namespace pms
