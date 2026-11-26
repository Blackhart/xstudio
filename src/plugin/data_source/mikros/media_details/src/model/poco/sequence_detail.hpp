// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/sequence_detail.hpp
 * @brief Sequence row with its project parent for Media Details.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_media_details::poco {

/**
 * @brief One sequence as shown in Media Details, including the project parent.
 */
struct SequenceDetail {
    std::int64_t id = 0;          ///< Domain sequence id.
    std::string name;             ///< Domain sequence name.
    std::int64_t project_id = 0;  ///< Linked project id. 0 when none.
    std::string project_name;     ///< Linked project name.
};

} // namespace xstudio::mikros_media_details::poco
