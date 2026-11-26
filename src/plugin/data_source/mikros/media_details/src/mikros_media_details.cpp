// SPDX-License-Identifier: Apache-2.0
/**
 * @file mikros_media_details.cpp
 * @brief Panel actor: construction and relay of attributes and events.
 */

#include "mikros_media_details.hpp"

#include "model/dal/dal.hpp"
#include "viewModel/media_details.hpp"

#include "xstudio/plugin_manager/plugin_factory.hpp"

using namespace xstudio;
using namespace xstudio::mikros_media_details;

MikrosMediaDetails::MikrosMediaDetails(
    caf::actor_config &cfg, const utility::JsonStore &init_settings)
    : plugin::StandardPlugin(cfg, "MikrosMediaDetailsPlugin", init_settings) {

    dal::initialize(); ///< Shared pms session before the screen loads.
    media_details_ = std::make_unique<MediaDetails>(*this);
    make_behavior(); ///< Sets the actor address the attributes already use.
    connect_to_ui();
    listen_to_playhead_events(true); ///< Preview and playlist share the same on-screen media.
    register_ui_panel_qml(
        "Media Details",
        R"(
            import MikrosMediaDetails 1.0
            MediaDetailsPanel {
                anchors.fill: parent
            }
        )",
        99.0f,
        "qrc:/mikros_media_details_icons/mikros_media_details.svg",
        5.1f);
}

MikrosMediaDetails::~MikrosMediaDetails() = default;

void MikrosMediaDetails::attribute_changed(
    const utility::Uuid &attr_uuid, const int role) {
    if (media_details_) {
        media_details_->attribute_changed(attr_uuid, role);
    }
    plugin::StandardPlugin::attribute_changed(attr_uuid, role);
}

void MikrosMediaDetails::on_screen_media_changed(
    caf::actor media,
    const utility::MediaReference &media_ref,
    const utility::JsonStore & /*colour_params*/) {
    if (media_details_) {
        media_details_->on_screen_media_changed(media, media_ref);
    }
}

caf::message_handler MikrosMediaDetails::message_handler_extensions() {
    if (!media_details_) {
        return {};
    }
    return media_details_->message_handler_extensions();
}

XSTUDIO_PLUGIN_DECLARE_BEGIN()

XSTUDIO_REGISTER_PLUGIN(
    MikrosMediaDetails,
    MikrosMediaDetails::PLUGIN_UUID,
    MikrosMediaDetails,
    plugin_manager::PluginFlags::PF_DATA_SOURCE,
    true,
    Mikros,
    Mikros Media Details panel.,
    0.1.0)

XSTUDIO_PLUGIN_DECLARE_END()
