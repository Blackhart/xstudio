// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/media_details.cpp
 * @brief Media Details screen: stamp, CAF worker load, DetailJson.
 */

#include "viewModel/media_details.hpp"

#include "model/dal/dal.hpp"
#include "model/log.hpp"
#include "utility/detail_load.hpp"
#include "utility/thumbnails.hpp"

#include "pms/env.hpp"

#include "xstudio/atoms.hpp"
#include "xstudio/utility/helpers.hpp"

#include <caf/actor_cast.hpp>
#include <caf/actor_system.hpp>
#include <caf/event_based_actor.hpp>

#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>

#include <cstring>
#include <utility>
#include <vector>

extern char **environ;

namespace xstudio::mikros_media_details {
namespace {

constexpr const char *kAttrsGroup = "mikros_media_details_attributes";

/**
 * @brief Escapes quotes and backslashes for a quoted log field.
 * @param text Raw URL.
 * @return Text safe to wrap in double quotes.
 */
std::string escapeQuoted(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

/**
 * @brief Opens one page in the desktop browser.
 * @param url Absolute address. Passed as an argument, not through a shell.
 *
 * The child does not inherit `LD_LIBRARY_PATH`. A debug session points that
 * variable at vcpkg and Qt, and `xdg-open` then fails to load the system cairo.
 * Its standard streams go to `/dev/null`: Chrome writes zygote errors on the
 * inherited debugger pipes.
 */
void openUrlInBrowser(const std::string &url) {
    std::vector<std::string> kept;
    for (char **entry = environ; entry && *entry; ++entry) {
        if (std::strncmp(*entry, "LD_LIBRARY_PATH=", 16) == 0) {
            continue; ///< System opener must use the system libraries.
        }
        kept.emplace_back(*entry);
    }
    std::vector<char *> envp;
    envp.reserve(kept.size() + 1);
    for (std::string &entry : kept) {
        envp.push_back(entry.data());
    }
    envp.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP); ///< Leave the debugger process group.
    posix_spawnattr_setpgroup(&attr, 0);

    char *argv[] = {const_cast<char *>("xdg-open"), const_cast<char *>(url.c_str()), nullptr};
    pid_t pid = 0;
    const int error = posix_spawnp(&pid, "xdg-open", &actions, &attr, argv, envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (error != 0) {
        log::error(std::string("ui open shotgrid browser failed: ") + std::strerror(error));
    }
}

/**
 * @brief Panel actor pointer from the StandardPlugin reference.
 * @param panel Plugin actor that owns the screen.
 * @return Strong actor handle for the panel.
 */
caf::actor panelActor(plugin::StandardPlugin &panel) {
    return caf::actor_cast<caf::actor>(static_cast<caf::event_based_actor *>(&panel));
}

/**
 * @brief Strips the private `_thumb` helper object before publishing.
 * @param payload Worker JSON that may contain `_thumb`.
 * @return Copy without `_thumb`.
 */
nlohmann::json publicDetailJson(nlohmann::json payload) {
    if (payload.is_object()) {
        payload.erase("_thumb");
    }
    return payload;
}

/**
 * @brief Turns one field style into the object QML looks up.
 * @param style Colours, labels, and the fallback colour.
 * @return `{colors, labels, defaultColor}`.
 */
nlohmann::json styleJson(const dal::StatusStyle &style) {
    nlohmann::json colors = nlohmann::json::object();
    for (const auto &entry : style.colors) {
        colors[entry.first] = entry.second; ///< One ShotGrid code.
    }
    nlohmann::json labels = nlohmann::json::object();
    for (const auto &entry : style.labels) {
        labels[entry.first] = entry.second;
    }
    return nlohmann::json{
        {"colors", std::move(colors)},
        {"labels", std::move(labels)},
        {"defaultColor", style.defaultColor}};
}

/**
 * @brief Status styles the detail panel shows.
 * @return sequence, shot, asset, and both version status fields.
 */
nlohmann::json detailStatusStyles() {
    nlohmann::json styles = nlohmann::json::object();
    styles["sequence"] = {{"status", styleJson(dal::statusStyle("sequence", "status"))}};
    styles["shot"] = {{"status", styleJson(dal::statusStyle("shot", "status"))}};
    styles["asset"] = {{"status", styleJson(dal::statusStyle("asset", "status"))}};
    nlohmann::json version = nlohmann::json::object();
    version["status"] = styleJson(dal::statusStyle("version", "status"));
    version["artistic_status"] = styleJson(dal::statusStyle("version", "artistic_status"));
    styles["version"] = std::move(version); ///< Technical and artistic stay separate.
    return styles;
}

} // namespace

MediaDetails::MediaDetails(plugin::StandardPlugin &panel) : QObject(nullptr), panel_(panel) {
    initQmlAttributes(panel);
    publishStatusStyles();
    setDefaultValues();
}

MediaDetails::~MediaDetails() = default;

void MediaDetails::initQmlAttributes(plugin::StandardPlugin &panel) {
    detail_state_ = panel.add_string_attribute("DetailState", "DetailState", "empty");
    detail_state_->expose_in_ui_attrs_group(kAttrsGroup);
    detail_error_ = panel.add_string_attribute("DetailError", "DetailError", "");
    detail_error_->expose_in_ui_attrs_group(kAttrsGroup);
    detail_json_ = panel.add_json_attribute("DetailJson", "DetailJson", nlohmann::json::object());
    detail_json_->expose_in_ui_attrs_group(kAttrsGroup);
    status_styles_ =
        panel.add_json_attribute("StatusStyles", "StatusStyles", nlohmann::json::object());
    status_styles_->expose_in_ui_attrs_group(kAttrsGroup);
    shotgrid_base_url_ = panel.add_string_attribute("ShotgridBaseUrl", "ShotgridBaseUrl", "");
    shotgrid_base_url_->expose_in_ui_attrs_group(kAttrsGroup);
    shotgrid_base_url_->set_preference_path(
        "/plugin/data_source/mikros_media_details/shotgrid_url");
    shotgrid_env_url_ = panel.add_string_attribute("ShotgridEnvUrl", "ShotgridEnvUrl", "");
    shotgrid_env_url_->expose_in_ui_attrs_group(kAttrsGroup); ///< Not a preference: prefs must not clear it.
    open_shotgrid_url_ = panel.add_string_attribute("OpenShotgridUrl", "OpenShotgridUrl", "");
    open_shotgrid_url_->expose_in_ui_attrs_group(kAttrsGroup);
}

void MediaDetails::publishStatusStyles() {
    status_styles_->set_value(detailStatusStyles()); ///< Cards are fixed for the process.
}

void MediaDetails::setDefaultValues() {
    const pms::TextSetting envUrl = pms::shotGridUrl();
    if (envUrl.set && !envUrl.value.empty() && shotgrid_env_url_) {
        shotgrid_env_url_->set_value(envUrl.value); ///< Preference stays empty and still means "use env".
    }
}

void MediaDetails::attribute_changed(const utility::Uuid &attr_uuid, int role) {
    if (role != module::Attribute::Value) {
        return; ///< Only a new value is a panel edit this screen handles.
    }
    if (open_shotgrid_url_ && attr_uuid == open_shotgrid_url_->uuid()) {
        openShotgridUrl(open_shotgrid_url_->value()); ///< Opens the page outside the debug library path.
    }
}

void MediaDetails::openShotgridUrl(const std::string &url) {
    if (url.empty()) {
        return; ///< The clear before a repeat click is not a new link.
    }
    // Names the ShotGrid page the user clicked.
    log::info("ui open shotgrid url=\"" + escapeQuoted(url) + "\"");
    openUrlInBrowser(url); ///< Qt.openUrlExternally inherits LD_LIBRARY_PATH and fails.
}

void MediaDetails::sendDetailsToQml(
    const nlohmann::json &detail, const std::string &state, const std::string &error) {
    detail_json_->set_value(detail);
    detail_state_->set_value(state);
    detail_error_->set_value(error);
}

void MediaDetails::clearDetails() {
    sendDetailsToQml(nlohmann::json::object(), "empty");
}

void MediaDetails::showNotMikros(const nlohmann::json &mediaDetails) {
    nlohmann::json json = nlohmann::json::object();
    json["media"] = mediaDetails;
    sendDetailsToQml(std::move(json), "not_mikros");
}

void MediaDetails::loadVersionDetailsFromIdAsync(
    std::int64_t versionId,
    const nlohmann::json &mediaDetails,
    std::uint64_t generation) {
    MediaDetailLoadRequest request;
    request.panel = caf::actor_cast<caf::weak_actor_ptr>(panelActor(panel_));
    request.request_id = generation;
    request.version_id = versionId;
    request.media_details = utility::JsonStore(mediaDetails);
    launchMediaDetailLoad(panel_.system(), std::move(request)); ///< Token mail, not a new CAF type.
}

void MediaDetails::on_screen_media_changed(
    caf::actor media, const utility::MediaReference &media_ref) {
    // Invalidate in-flight loads first, then clear QML (clearDetails does not bump).
    const std::uint64_t generation = generation_.fetch_add(1) + 1;
    clearDetails();

    if (!media) {
        return;
    }

    const nlohmann::json mediaDetails = loadMediaDetails(media_ref);
    nlohmann::json loading = nlohmann::json::object();
    loading["media"] = mediaDetails;
    sendDetailsToQml(std::move(loading), "loading");

    panel_
        .mail(
            json_store::get_json_atom_v,
            utility::Uuid(),
            std::string(kMikrosVersionMetaPath))
        .request(media, caf::infinite)
        .then(
            [=](const utility::JsonStore &meta) {
                if (generation != generation_.load()) {
                    return; ///< A newer selection already replaced this load.
                }
                const std::optional<std::int64_t> versionId = versionIdFromStamp(meta);
                if (!versionId) {
                    showNotMikros(mediaDetails);
                    return;
                }
                log::info(
                    "ui select media version=" +
                    std::to_string(*versionId)); ///< The detail find follows this media change.
                loadVersionDetailsFromIdAsync(*versionId, mediaDetails, generation);
            },
            [=](const caf::error &) {
                if (generation != generation_.load()) {
                    return;
                }
                showNotMikros(mediaDetails);
            });
}

void MediaDetails::applyThumbnail(
    std::uint64_t generation,
    std::int64_t projectId,
    std::int64_t versionId,
    std::string image,
    std::string imageProvider) {
    const caf::actor panel = panelActor(panel_);
    scheduleOneThumb(
        projectId,
        versionId,
        std::move(image),
        std::move(imageProvider),
        [panel, generation](std::string fileUrl) {
            anon_mail(
                data_source::put_data_atom_v,
                generation,
                std::string("version_thumb"),
                utility::JsonStore(nlohmann::json{{"image", std::move(fileUrl)}}))
                .send(panel);
        });
}

void MediaDetails::applyThumbUrl(const std::string &fileUrl) {
    if (fileUrl.empty() || !detail_json_) {
        return;
    }
    nlohmann::json json = detail_json_->value();
    if (!json.is_object()) {
        json = nlohmann::json::object();
    }
    if (!json.contains("files") || !json["files"].is_object()) {
        json["files"] = nlohmann::json::object();
    }
    json["files"]["image"] = fileUrl;
    detail_json_->set_value(std::move(json));
}

void MediaDetails::applyWorkerPage(
    std::uint64_t generation, const std::string &catalog, const utility::JsonStore &payload) {
    if (generation != generation_.load()) {
        return;
    }
    if (catalog == "version_thumb") {
        applyThumbUrl(payload.value("image", std::string{}));
        return;
    }
    if (catalog == "version_detail_error") {
        nlohmann::json json = nlohmann::json::object();
        json["version"] = {{"id", payload.value("id", std::int64_t{0})}};
        sendDetailsToQml(
            std::move(json),
            "error",
            payload.value("message", std::string{"Load failed"}));
        return;
    }
    if (catalog != "version_detail") {
        return;
    }

    std::int64_t thumbProjectId = 0;
    std::int64_t thumbVersionId = 0;
    std::string thumbImage;
    std::string thumbProvider;
    if (payload.contains("_thumb") && payload["_thumb"].is_object()) {
        const auto &thumb = payload["_thumb"];
        thumbProjectId = thumb.value("projectId", std::int64_t{0});
        thumbVersionId = thumb.value("versionId", std::int64_t{0});
        thumbImage = thumb.value("image", std::string{});
        thumbProvider = thumb.value("imageProvider", std::string{});
    }

    sendDetailsToQml(publicDetailJson(payload), "ready");
    applyThumbnail(
        generation, thumbProjectId, thumbVersionId, std::move(thumbImage), std::move(thumbProvider));
}

} // namespace xstudio::mikros_media_details
