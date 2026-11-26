// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/media_meta.hpp
 * @brief Stamp path and playhead media fields for Media Details.
 */
#pragma once

#include "xstudio/utility/json_store.hpp"
#include "xstudio/utility/media_reference.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace xstudio::mikros_media_details {

/**
 * @brief JSON path of the Mikros version stamp on a media actor.
 */
inline constexpr const char *kMikrosVersionMetaPath = "/metadata/mikros/version";

/**
 * @brief Reads the ShotGrid version id from stamp JSON.
 * @param meta JSON at `/metadata/mikros/version`.
 * @return The id when present and positive.
 */
std::optional<std::int64_t> versionIdFromStamp(const utility::JsonStore &meta);

/**
 * @brief Builds the Media section JSON from a playhead media reference.
 *
 * Branches on media type:
 * - `sequence`: `path`, `frameRange`, `frameCount`
 * - `video`: `path`, `container`, `codec`, `fps`, `frameCount`, `duration`
 *
 * `codec` stays empty until a richer media metadata source is wired.
 *
 * @param media_ref On-screen media reference.
 * @return Object under `media` for DetailJson.
 */
nlohmann::json loadMediaDetails(const utility::MediaReference &media_ref);

} // namespace xstudio::mikros_media_details
