// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/log.cpp
 * @brief Formats Media Details trace lines for spdlog.
 */

#include "model/log.hpp"

#include "xstudio/utility/logging.hpp"

#include <string>

namespace xstudio::mikros_media_details::log {

void info(std::string_view message) {
    spdlog::info("[mikros] media_details {}", message);
}

void error(std::string_view message) {
    spdlog::error("[mikros] media_details {}", message);
}

void failure(const std::exception &caught) {
    const std::string text = caught.what();
    if (text.rfind("shotgrid:", 0) == 0) {
        error(std::string("shotgrid error ") + text);
        return;
    }
    error(std::string("dal error ") + text);
}

} // namespace xstudio::mikros_media_details::log
