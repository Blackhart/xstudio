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
    std::string description;      ///< Sequence description.
    std::string status;           ///< ShotGrid status.
    std::int64_t cut_duration_in_sec = 0;    ///< Cut duration in seconds.
    std::int64_t estimated_frames_count = 0; ///< Estimated frame count.
    std::int64_t estimated_shots_count = 0;  ///< Estimated shot count.
    std::int64_t shot_count = 0;             ///< Current shot count.
    std::int64_t sequence_order = 0;         ///< Sequence order.
    std::int64_t project_id = 0;  ///< Linked project id. 0 when none.
    std::string project_name;     ///< Linked project name.
};

} // namespace xstudio::mikros_media_details::poco
