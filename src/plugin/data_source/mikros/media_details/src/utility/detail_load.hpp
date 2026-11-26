// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/detail_load.hpp
 * @brief Detached CAF actor that loads version + parent tree for Media Details.
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
#include <string>

namespace xstudio::mikros_media_details {

/**
 * @brief One media-detail load. The actor stops after it finishes this request.
 *
 * The panel still drops a payload whose request id is no longer current.
 */
struct MediaDetailLoadRequest {
    caf::weak_actor_ptr panel;     ///< Panel that receives the payload.
    std::uint64_t request_id = 0;  ///< Id the panel checks before applying.
    std::int64_t version_id = 0;   ///< ShotGrid version id to load.
    utility::JsonStore media_details; ///< Media section from `loadMediaDetails`.
};

} // namespace xstudio::mikros_media_details

CAF_ALLOW_UNSAFE_MESSAGE_TYPE(xstudio::mikros_media_details::MediaDetailLoadRequest)

// Local only: the request holds a weak actor pointer, so it is not serialized.
// 17100 stays clear of the Shot Browser catalog block at 17000.
CAF_BEGIN_TYPE_ID_BLOCK(mikros_media_details_load, FIRST_CUSTOM_ID + 17100)
CAF_ADD_TYPE_ID(mikros_media_details_load, (xstudio::mikros_media_details::MediaDetailLoadRequest))
CAF_END_TYPE_ID_BLOCK(mikros_media_details_load)

namespace xstudio::mikros_media_details {

/**
 * @brief Loads one version detail graph on its own thread, then stops.
 *
 * Spawned detached so a blocking find does not occupy the panel actor.
 * It is not linked to the panel: a failed load does not stop the panel.
 */
class MediaDetailLoadActor : public caf::event_based_actor {
  public:
    /**
     * @brief Builds an actor that waits for one load request.
     * @param cfg CAF actor config.
     */
    explicit MediaDetailLoadActor(caf::actor_config &cfg);

    /** @brief Name shown in CAF logs. */
    const char *name() const override;

    /**
     * @brief Runs the load, then quits. A failure is logged and does not stop the panel.
     * @return The one-shot behavior.
     */
    caf::behavior make_behavior() override;
};

/**
 * @brief Runs the DAL finds, walks parents, and mails the panel.
 * @param request Version id, request id, and media JSON.
 */
void executeMediaDetailLoad(const MediaDetailLoadRequest &request);

/**
 * @brief Registers `MediaDetailLoadRequest` with CAF. Call once before the first load.
 */
void registerMediaDetailLoadType();

} // namespace xstudio::mikros_media_details
