// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/shared_session.hpp
 * @brief Process-wide Session and CachingBackend shared by Mikros plugins.
 */
#pragma once

#include "pms/session.hpp"

#include <functional>
#include <string_view>

namespace pms {

/**
 * @brief Installs the info sink used by cache and ShotGrid traces.
 *
 * Call before the first `sharedSession()` use when traces should reach a host
 * logger. An empty function disables tracing. The sink is read on each log
 * line, so it may be set before or after the session is created.
 *
 * @param log Receives one message body (no `[mikros]` prefix).
 */
void setSharedInfoLog(std::function<void(std::string_view)> log);

/**
 * @brief Returns the process-wide session (schema + CachingBackend).
 *
 * Backend comes from `MIKROS_PMS_BACKEND`. The cache is one instance for every
 * plugin that links this shared library.
 *
 * @return Session bound to the shared cache.
 * @throw Error When ShotGrid is selected and an env var is missing, or the
 *     schema path cannot be loaded.
 */
Session &sharedSession();

} // namespace pms
