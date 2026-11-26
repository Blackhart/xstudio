// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/project_detail.hpp
 * @brief Project row for Media Details.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_media_details::poco {

/**
 * @brief One project as shown in Media Details.
 */
struct ProjectDetail {
    std::int64_t id = 0; ///< Domain project id.
    std::string name;    ///< Domain project name.
    std::string code;    ///< Project code.
    std::string status;  ///< ShotGrid status.
    std::string type;    ///< ShotGrid project type.
};

} // namespace xstudio::mikros_media_details::poco
