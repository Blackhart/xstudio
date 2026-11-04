// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/page.hpp
 * @brief One page of find results with an optional continuation cursor.
 */
#pragma once

#include "pms/value.hpp"

#include <optional>
#include <string>

namespace pms {

/**
 * @brief Rows returned by find(), optionally one keyset page.
 *
 * Without a limit, `rows` holds every match and `next_cursor` is absent. With a
 * limit, `next_cursor` is absent on the last page; pass it to Query::After() to
 * continue. There is no total count in this slice.
 */
struct Page {
    Records rows; ///< Rows in this page, in backend order.
    std::optional<std::string> next_cursor; ///< Opaque token for the next page.
};

} // namespace pms
