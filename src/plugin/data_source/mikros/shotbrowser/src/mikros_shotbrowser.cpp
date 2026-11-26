// SPDX-License-Identifier: Apache-2.0
/**
 * @file mikros_shotbrowser.cpp
 * @brief Panel actor: construction, attributes, and the relay that delivers each page to its screen.
 */

#include "mikros_shotbrowser.hpp"
#include "utility/media.hpp"
#include "utility/thumbnails.hpp"
#include "viewModel/shot_browser.hpp"

#include "model/constants.hpp"

#include "pms/env.hpp"

#include "xstudio/atoms.hpp"
#include "xstudio/plugin_manager/plugin_factory.hpp"
#include "xstudio/session/session.hpp"
#include "xstudio/utility/helpers.hpp"

#include <caf/actor_registry.hpp>

#include <filesystem>

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace {

/**
 * @brief Writes `set` or `default` after a resolved setting.
 * @param set True when the environment variable is non-empty.
 * @return The origin word.
 */
const char *settingOrigin(bool set) { return set ? "set" : "default"; }

/**
 * @brief Formats one text setting for the launch log.
 * @param name Environment variable name.
 * @param setting Resolved value.
 * @return `name=value (set)`, `name=value (default)`, or `name=unset`.
 */
std::string textSettingField(const char *name, const pms::TextSetting &setting) {
    if (!setting.set && setting.value.empty()) {
        return std::string(name) + "=unset";
    }
    return std::string(name) + "=" + setting.value + " (" + settingOrigin(setting.set) + ")";
}

/**
 * @brief Formats one numeric setting for the launch log.
 * @param name Environment variable name.
 * @param setting Resolved value.
 * @return `name=value (set)` or `name=value (default)`.
 */
std::string numberSettingField(const char *name, const pms::NumberSetting &setting) {
    return std::string(name) + "=" + std::to_string(setting.value) + " (" +
           settingOrigin(setting.set) + ")";
}

/**
 * @brief Formats one Shot Browser numeric setting for the launch log.
 * @param name Environment variable name.
 * @param setting Resolved value.
 * @return `name=value (set)` or `name=value (default)`.
 */
std::string numberSettingField(const char *name, const constants::NumberSetting &setting) {
    return std::string(name) + "=" + std::to_string(setting.value) + " (" +
           settingOrigin(setting.set) + ")";
}

/**
 * @brief Formats the ShotGrid script key without printing the secret.
 * @return `SHOTGRID_CLIENT_SECRET=unset`, or the redacted key marked `set`.
 */
std::string secretSettingField() {
    const pms::TextSetting secret = pms::shotGridClientSecret();
    if (!secret.set) {
        return std::string(pms::kShotGridClientSecret) + "=unset";
    }
    return std::string(pms::kShotGridClientSecret) + "=" + pms::keyHint(secret.value) + " (set)";
}

/**
 * @brief Logs every Shot Browser setting and whether it came from the environment.
 */
void logSettings() {
    trace::info(
        std::string("env ") + textSettingField(pms::kBackend, pms::backend()) + " " +
        numberSettingField(pms::kCacheTtlSeconds, pms::cacheTtlSeconds()) + " " +
        numberSettingField(pms::kCacheBytes, pms::cacheMaxBytes()) + " " +
        numberSettingField(constants::kPageSize, constants::pageSize()) + " " +
        textSettingField(pms::kSchemaPath, pms::schemaPath()) + " " +
        textSettingField(pms::kShotGridUrl, pms::shotGridUrl()) + " " +
        textSettingField(pms::kShotGridClientId, pms::shotGridClientId()) + " " +
        textSettingField(pms::kShotGridMapping, pms::shotGridMapping()) + " " +
        secretSettingField());
}

/**
 * @brief Logs every path the Shot Browser writes.
 */
void logWriteLocations() {
    const std::string preferences = utility::preference_path_context("PLUGIN");
    const std::filesystem::path root = thumb::thumbRoot();
    if (root.empty()) {
        trace::info(
            "writes preferences " + preferences +
            " (browser_mode, result_view, left_pane_width); thumbnail directory unavailable "
            "because HOME is not set");
        return;
    }
    trace::info(
        "writes preferences " + preferences +
        " (browser_mode, result_view, left_pane_width); thumbnails " + root.string());
}

} // namespace

MikrosShotBrowser::MikrosShotBrowser(
    caf::actor_config &cfg, const utility::JsonStore &init_settings)
    : plugin::StandardPlugin(cfg, "MikrosShotBrowserPlugin", init_settings) {

    dal::initialize(); ///< The shot browser context exists before any screen reads it.
    shot_browser_ = std::make_unique<ShotBrowser>(*this);
    logSettings();       ///< Each environment variable, and whether it is set or default.
    logWriteLocations(); ///< Preferences file and thumbnail directory, resolved now.
    make_behavior(); ///< Sets the actor address the attributes already use.
    connect_to_ui();
    joinPreviewFocusEvents(); ///< Drop `preview` when the viewport leaves that playlist.
    register_ui_panel_qml(
        "Mikros Shot Browser",
        R"(
            import MikrosShotBrowser 1.0
            MikrosShotBrowserPanel {
                anchors.fill: parent
            }
        )",
        100.0f,
        "qrc:/mikros_shotbrowser_icons/mikros_shotbrowser.svg",
        5.0f);
}

MikrosShotBrowser::~MikrosShotBrowser() = default;

void MikrosShotBrowser::attribute_changed(
    const utility::Uuid &attr_uuid, const int role) {
    if (role != module::Attribute::Value) {
        plugin::StandardPlugin::attribute_changed(attr_uuid, role);
        return;
    }
    if (shot_browser_) {
        shot_browser_->attribute_changed(attr_uuid, role); ///< The screen that owns the attribute.
    }
    plugin::StandardPlugin::attribute_changed(attr_uuid, role);
}

void MikrosShotBrowser::joinPreviewFocusEvents() {
    auto global = system().registry().template get<caf::actor>(global_registry);
    if (!global) {
        return;
    }
    mail(session::session_atom_v)
        .request(global, caf::infinite)
        .then(
            [=](caf::actor session) {
                if (session) {
                    utility::join_event_group(this, session);
                }
            },
            [=](const caf::error &) {});
}

caf::message_handler MikrosShotBrowser::message_handler_extensions() {
    caf::message_handler preview_focus{
        [=](utility::event_atom,
            session::viewport_active_media_container_atom,
            const utility::UuidActor &viewed) {
            onPreviewViewportContainerChanged(*this, viewed);
        }};
    if (!shot_browser_) {
        return preview_focus;
    }
    return preview_focus.or_else(shot_browser_->message_handler_extensions());
}

XSTUDIO_PLUGIN_DECLARE_BEGIN()

XSTUDIO_REGISTER_PLUGIN(
    MikrosShotBrowser,
    MikrosShotBrowser::PLUGIN_UUID,
    MikrosShotBrowser,
    plugin_manager::PluginFlags::PF_DATA_SOURCE,
    true,
    Mikros,
    Mikros Shot Browser panel.,
    0.1.0)

XSTUDIO_PLUGIN_DECLARE_END()
