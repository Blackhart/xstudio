// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/media_fetch.hpp
 * @brief Downloads one signed movie URL.
 */
#pragma once

#include <string>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Downloads `url` and returns the body bytes.
 * @param url Signed `sg_uploaded_movie_mp4` address.
 * @return The body, or empty when the request fails.
 */
std::string fetchMovie(const std::string &url);

} // namespace xstudio::mikros_shotbrowser
