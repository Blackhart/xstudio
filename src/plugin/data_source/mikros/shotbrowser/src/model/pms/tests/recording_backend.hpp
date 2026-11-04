// SPDX-License-Identifier: Apache-2.0
/**
 * @file recording_backend.hpp
 * @brief Backend double that records the query and returns rows prepared by the test.
 */
#pragma once

#include "pms/backend.hpp"

#include <vector>

namespace pms {

/**
 * @brief Stores each find query and answers with `next`.
 *
 * The returned rows are already in domain vocabulary. Set `next` before calling
 * find(). The response is wrapped in a Page with no next_cursor.
 */
class RecordingBackend : public Backend {
  public:
    Records next;          ///< Rows returned by the next find, then kept as-is.
    std::vector<Ir> calls; ///< Queries received, in call order.

    /**
     * @brief Records `ir` and returns `next` as a single page.
     * @param ir Validated find query.
     * @return Page whose rows are a copy of `next` and whose cursor is empty.
     */
    Page find(const Ir &ir) override {
        calls.push_back(ir);
        Page page;
        page.rows = next;
        return page;
    }
};

} // namespace pms
