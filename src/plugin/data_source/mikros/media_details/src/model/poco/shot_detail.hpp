// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/shot_detail.hpp
 * @brief Shot row with its sequence parent for Media Details.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_media_details::poco {

/**
 * @brief One shot as shown in Media Details, including the sequence parent.
 */
struct ShotDetail {
    std::int64_t id = 0;           ///< Domain shot id.
    std::string name;              ///< Domain shot name.
    std::string description;       ///< Shot description.
    std::string status;            ///< ShotGrid status.
    std::int64_t cut_duration_in_sec = 0; ///< Cut duration in seconds.
    std::int64_t cut_in = 0;              ///< Cut in frame.
    std::int64_t cut_out = 0;             ///< Cut out frame.
    std::int64_t cut_order = 0;           ///< Cut order.
    std::int64_t sequence_id = 0;  ///< Linked sequence id. 0 when none.
    std::string sequence_name;     ///< Linked sequence name.
};

} // namespace xstudio::mikros_media_details::poco
