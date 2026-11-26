// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/notes_load.hpp
 * @brief Detached CAF actor that loads the notes of one version.
 */
#pragma once

#include "xstudio/utility/json_store.hpp"

#include <caf/actor_system.hpp>
#include <caf/event_based_actor.hpp>
#include <caf/fwd.hpp>

#include <cstdint>

namespace xstudio::mikros_media_details {

/**
 * @brief One notes load. The actor stops after it finishes this request.
 *
 * The panel still drops a payload whose request id is no longer current.
 */
struct NotesLoadRequest {
    caf::weak_actor_ptr panel;    ///< Panel that receives the payload.
    std::uint64_t request_id = 0; ///< Id the panel checks before applying.
    std::int64_t version_id = 0;  ///< ShotGrid version id whose notes to load.
};

} // namespace xstudio::mikros_media_details

namespace xstudio::mikros_media_details {

/**
 * @brief Loads one version's notes on its own thread, then stops.
 *
 * Spawned detached so a blocking find does not occupy the panel actor.
 * It is not linked to the panel: a failed load does not stop the panel.
 */
class NotesLoadActor : public caf::event_based_actor {
  public:
    /**
     * @brief Builds an actor that waits for one load request.
     * @param cfg CAF actor config.
     */
    explicit NotesLoadActor(caf::actor_config &cfg);

    /** @brief Name shown in CAF logs. */
    const char *name() const override;

    /**
     * @brief Runs the load, then quits. A failure is logged and does not stop the panel.
     * @return The one-shot behavior.
     */
    caf::behavior make_behavior() override;
};

/**
 * @brief Runs the notes find and mails the panel.
 * @param request Version id and request id.
 */
void executeNotesLoad(const NotesLoadRequest &request);

/**
 * @brief Spawns one detached load and sends it the request token.
 *
 * The request stays in this process. The message is a `uint64_t`, a CAF type
 * registered before the actor system starts. The actor is not linked to the panel.
 *
 * @param system Actor system that owns the panel.
 * @param request Version id and request id.
 */
void launchNotesLoad(caf::actor_system &system, NotesLoadRequest request);

} // namespace xstudio::mikros_media_details
