// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/backend.hpp
 * @brief Execution target for a validated query.
 */
#pragma once

#include "pms/ir.hpp"
#include "pms/page.hpp"

#include <cstddef>

namespace pms {

/**
 * @brief Receives a domain query and returns domain rows or a match count.
 *
 * RecordingBackend is the unit-test double. FakeBackend lives under
 * src/pms/fake/ (see pms/dev/). ShotGridBackend lives under src/pms/shotgrid/.
 * This interface never sees ShotGrid names.
 *
 * When `ir.limit` is set, find() returns one keyset page and may set
 * `next_cursor`. When `limit` is absent, find() returns every matching row and
 * leaves `next_cursor` empty. count() never takes a limit or cursor.
 */
class Backend {
  public:
    /**
     * @brief Releases the backend.
     */
    virtual ~Backend() = default;

    /**
     * @brief Runs a validated find.
     * @param ir Query with `op` set to Find. `where` may be null. Optional
     *     `limit` / `cursor` select a page.
     * @return Page of matching rows in domain vocabulary.
     */
    virtual Page find(const Ir &ir) = 0;

    /**
     * @brief Runs a validated count.
     * @param ir Query with `op` set to Count. No `limit` / `cursor`.
     * @return How many rows match the same filters as find().
     */
    virtual std::size_t count(const Ir &ir) = 0;
};

} // namespace pms
