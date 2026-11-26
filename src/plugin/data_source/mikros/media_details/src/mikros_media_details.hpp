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

/**
 * @brief Plugin actor that builds the panel and forwards each event to MediaDetails.
 *
 * No screen logic lives here. Attribute changes, playhead events, and worker
 * pages go to the view model; the view model decides what to do.
 */
class MikrosMediaDetails : public plugin::StandardPlugin {
  public:
    inline static const utility::Uuid PLUGIN_UUID =
        utility::Uuid("c4e8a91b-3f2d-4b7a-9e15-6d0c8f2a1b47");

    /**
     * @brief Creates the panel actor and the MediaDetails screen.
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
     * @brief Forwards worker pages to MediaDetails.
     * @return Handlers merged into the plugin behavior.
     */
    caf::message_handler message_handler_extensions() override;

    /**
     * @brief Forwards a panel attribute change to MediaDetails.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, const int role) override;

    /**
     * @brief Forwards the on-screen media event to MediaDetails.
     * @param media On-screen media actor.
     * @param media_ref Path and frame data.
     * @param colour_params Unused colour pipeline params.
     */
    void on_screen_media_changed(
        caf::actor media,
        const utility::MediaReference &media_ref,
        const utility::JsonStore &colour_params) override;

    std::unique_ptr<MediaDetails> media_details_; ///< View model that owns the screen.
};

} // namespace xstudio::mikros_media_details
