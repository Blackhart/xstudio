// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/log.hpp
 * @brief One prefix for Media Details trace lines.
 */
#pragma once

#include <exception>
#include <string_view>

namespace xstudio::mikros_media_details::log {

/**
 * @brief Writes one info line prefixed with `[mikros]`.
 * @param message Text after the prefix.
 */
void info(std::string_view message);

/**
 * @brief Writes one error line prefixed with `[mikros]`.
 * @param message Text after the prefix.
 */
void error(std::string_view message);

/**
 * @brief Logs a failed find.
 * @param caught Exception caught around a DAL find.
 */
void failure(const std::exception &caught);

} // namespace xstudio::mikros_media_details::log
