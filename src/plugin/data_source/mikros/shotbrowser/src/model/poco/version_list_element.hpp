// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/poco/version_list_element.hpp
 * @brief Version card row for the list: metadata without image or media paths.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::poco {

/**
 * @brief One version as shown in the version list.
 *
 * Loaded with a find that omits `image` and media fields so ShotGrid does not
 * mint signed URLs for every row of the page.
 */
struct VersionListElement {
    std::int64_t id = 0;           ///< Domain version id.
    std::string name;              ///< Domain version name.
    std::string author;            ///< Person who created the version. Empty shows N/A.
    std::string created_at;        ///< Creation time as ShotGrid sent it. Empty shows N/A.
    std::string frame_range;       ///< Frame range text. Empty shows N/A.
    std::string description;       ///< Version description. Empty shows N/A.
    std::string step;              ///< Pipeline step name. Empty shows N/A.
    std::string status;            ///< Step status. Empty shows N/A.
    std::string artistic_status;   ///< Artistic status. Empty shows N/A.
    std::string sites;             ///< ShotGrid site names, joined by commas.
    bool has_notes = false;        ///< True when the version has at least one note.
    bool synced_to_client = false; ///< True when the version was sent to the client.
};

} // namespace xstudio::mikros_shotbrowser::poco
