// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/log.hpp
 * @brief One prefix for Shot Browser trace lines.
 */
#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace xstudio::mikros_shotbrowser::log {

/**
 * @brief Writes one info line prefixed with `[mikros]`.
 * @param message Text after the prefix. May contain newlines.
 */
void info(std::string_view message);

/**
 * @brief Writes one error line prefixed with `[mikros]`.
 * @param message Text after the prefix. May contain newlines.
 */
void error(std::string_view message);

/**
 * @brief Logs the current selection of one entity.
 *
 * An empty selection is `none`. A chosen row prints `id` and `name`.
 *
 * @param entity `project`, `sequence`, `shot`, or `asset`.
 * @param chosen False when the selector is cleared.
 * @param id Domain id. Ignored when `chosen` is false.
 * @param name Domain name. Ignored when `chosen` is false.
 */
void select(const char *entity, bool chosen, std::int64_t id, const std::string &name);

/**
 * @brief Logs a worker reply dropped because it no longer matches the UI.
 * @param catalog Catalog name on the message.
 * @param request Request id that was dropped.
 */
void drop(const std::string &catalog, std::uint64_t request);

/**
 * @brief Logs a failed find.
 *
 * Messages that start with `shotgrid:` become `shotgrid error`. HTML bodies
 * are replaced by their size. Anything else becomes `dal error`.
 *
 * @param failure Exception caught around a DAL find.
 */
void failure(const std::exception &failure);

} // namespace xstudio::mikros_shotbrowser::log
