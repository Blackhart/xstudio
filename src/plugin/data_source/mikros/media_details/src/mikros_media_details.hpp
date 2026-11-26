// SPDX-License-Identifier: Apache-2.0
/**
 * @file mikros_media_details.hpp
 * @brief Plugin actor registered with xSTUDIO. Forwards attributes and events only.
 */
#pragma once

#include "xstudio/plugin_manager/plugin_base.hpp"
#include "xstudio/utility/media_reference.hpp"

#include <memory>

namespace xstudio::mikros_media_details {

class MediaDetails;
class VersionNotes;

/**
 * @brief Plugin actor that builds the panel and forwards each event to its screens.
 *
 * No screen logic lives here. Attribute changes go to Media Details.
 * Playhead events go to both screens. A worker page goes to the screen
 * named by its catalog.
 */
class MikrosMediaDetails : public plugin::StandardPlugin {
  public:
    inline static const utility::Uuid PLUGIN_UUID =
        utility::Uuid("c4e8a91b-3f2d-4b7a-9e15-6d0c8f2a1b47");

    /**
     * @brief Creates the panel actor and the two screens.
     * @param cfg CAF actor config.
     * @param init_settings Plugin init JSON from xSTUDIO.
     */
    MikrosMediaDetails(caf::actor_config &cfg, const utility::JsonStore &init_settings);

    /**
     * @brief Drops the actor. In-flight loads are ignored because the panel is gone.
     */
    ~MikrosMediaDetails() override;

  private:
    /**
     * @brief Forwards a worker page to the screen named by its catalog.
     *
     * `version_notes` and `version_notes_error` go to VersionNotes.
     * Every other catalog goes to MediaDetails. One handler consumes the
     * message; a second handler of the same signature would never run.
     * @return The page handler merged into the plugin behavior.
     */
    caf::message_handler message_handler_extensions() override;

    /**
     * @brief Forwards a panel attribute change to MediaDetails.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, const int role) override;

    /**
     * @brief Forwards the on-screen media event to both screens.
     * @param media On-screen media actor.
     * @param media_ref Path and frame data.
     * @param colour_params Unused colour pipeline params.
     */
    void on_screen_media_changed(
        caf::actor media,
        const utility::MediaReference &media_ref,
        const utility::JsonStore &colour_params) override;

    std::unique_ptr<MediaDetails> media_details_; ///< View model of the Details tab.
    std::unique_ptr<VersionNotes> version_notes_; ///< View model of the Notes tab.
};

} // namespace xstudio::mikros_media_details
