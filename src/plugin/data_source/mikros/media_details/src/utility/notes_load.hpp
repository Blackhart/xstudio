// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/notes_load.hpp
 * @brief Detached CAF actor that loads the notes of one version.
 */
#pragma once

#include "xstudio/atoms.hpp"
#include "xstudio/utility/json_store.hpp"

#include <caf/actor_system.hpp>
#include <caf/allowed_unsafe_message_type.hpp>
#include <caf/event_based_actor.hpp>
#include <caf/fwd.hpp>
#include <caf/type_id.hpp>

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

CAF_ALLOW_UNSAFE_MESSAGE_TYPE(xstudio::mikros_media_details::NotesLoadRequest)

// Local only: the request holds a weak actor pointer, so it is not serialized.
// 17101 stays clear of the detail-load block at 17100.
CAF_BEGIN_TYPE_ID_BLOCK(mikros_version_notes_load, FIRST_CUSTOM_ID + 17101)
CAF_ADD_TYPE_ID(mikros_version_notes_load, (xstudio::mikros_media_details::NotesLoadRequest))
CAF_END_TYPE_ID_BLOCK(mikros_version_notes_load)

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
 * @brief Registers `NotesLoadRequest` with CAF. Call once before the first load.
 */
void registerNotesLoadType();

} // namespace xstudio::mikros_media_details
