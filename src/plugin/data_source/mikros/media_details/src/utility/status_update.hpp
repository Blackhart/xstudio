// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/status_update.hpp
 * @brief Detached CAF actor that writes one editable status field.
 */
#pragma once

#include "xstudio/utility/json_store.hpp"

#include <caf/actor_system.hpp>
#include <caf/event_based_actor.hpp>
#include <caf/fwd.hpp>

#include <cstdint>
#include <string>

namespace xstudio::mikros_media_details {

/**
 * @brief One status write. The actor stops after it finishes this request.
 *
 * The panel drops a payload whose request id is no longer current.
 */
struct StatusUpdateRequest {
    caf::weak_actor_ptr panel; ///< Panel that receives the payload.
    std::uint64_t request_id = 0; ///< Generation the panel checks before applying.
    std::string entity; ///< Domain entity, such as `"shot"` or `"note"`.
    std::int64_t id = 0; ///< Record id.
    std::string field; ///< Domain field, such as `"status"`.
    std::string code; ///< Status code to store.
    std::string catalog_ok; ///< `status_updated` or `note_status_updated`.
    std::string catalog_error; ///< `status_error` or `note_status_error`.
};

/**
 * @brief Writes one status on its own thread, then stops.
 *
 * Spawned detached so a blocking update does not occupy the panel actor.
 * It is not linked to the panel: a failed write does not stop the panel.
 */
class StatusUpdateActor : public caf::event_based_actor {
  public:
    /**
     * @brief Builds an actor that waits for one update request.
     * @param cfg CAF actor config.
     */
    explicit StatusUpdateActor(caf::actor_config &cfg);

    /** @brief Name shown in CAF logs. */
    const char *name() const override;

    /**
     * @brief Runs the write, then quits. A failure is mailed and does not stop the panel.
     * @return The one-shot behavior.
     */
    caf::behavior make_behavior() override;
};

/**
 * @brief Writes the field, then mails success or the error text.
 * @param request Entity, id, field, code, and the catalogs to use.
 */
void executeStatusUpdate(const StatusUpdateRequest &request);

/**
 * @brief Spawns one detached update and sends it the request token.
 *
 * The request stays in this process. The message is a `uint64_t`. The actor
 * is not linked to the panel.
 *
 * @param system Actor system that owns the panel.
 * @param request Entity, id, field, and code.
 */
void launchStatusUpdate(caf::actor_system &system, StatusUpdateRequest request);

} // namespace xstudio::mikros_media_details
