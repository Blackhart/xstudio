// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/media_details.cpp
 * @brief Media Details screen: stamp, CAF worker load, DetailJson.
 */

#include "viewModel/media_details.hpp"

#include "utility/detail_load.hpp"
#include "utility/thumbnails.hpp"

#include "pms/env.hpp"

#include "xstudio/atoms.hpp"
#include "xstudio/utility/helpers.hpp"

#include <caf/actor_cast.hpp>
#include <caf/actor_system.hpp>
#include <caf/event_based_actor.hpp>

#include <utility>

namespace xstudio::mikros_media_details {
namespace {

constexpr const char *kAttrsGroup = "mikros_media_details_attributes";

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

} // namespace

MediaDetails::MediaDetails(plugin::StandardPlugin &panel) : QObject(nullptr), panel_(panel) {
    registerMediaDetailLoadType(); ///< Before the first detached load sends a request.
    initQmlAttributes(panel);
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
    shotgrid_base_url_ = panel.add_string_attribute("ShotgridBaseUrl", "ShotgridBaseUrl", "");
    shotgrid_base_url_->expose_in_ui_attrs_group(kAttrsGroup);
    shotgrid_base_url_->set_preference_path(
        "/plugin/data_source/mikros_media_details/shotgrid_url");
}

void MediaDetails::setDefaultValues() {
    const pms::TextSetting envUrl = pms::shotGridUrl();
    if (envUrl.set && !envUrl.value.empty() && shotgrid_base_url_) {
        shotgrid_base_url_->set_value(envUrl.value);
    }
}

void MediaDetails::attribute_changed(const utility::Uuid & /*attr_uuid*/, int role) {
    if (role != module::Attribute::Value) {
        return; ///< Only a new value is a panel edit this screen handles.
    }
    ///< ShotgridBaseUrl is preference-backed; no extra work.
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
    auto worker = panel_.system().spawn<MediaDetailLoadActor, caf::detached>();
    anon_mail(std::move(request)).send(worker);
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

void MediaDetails::updateMediaDetails(
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

caf::message_handler MediaDetails::message_handler_extensions() {
    return {
        [=](data_source::put_data_atom,
            std::uint64_t generation,
            const std::string &catalog,
            const utility::JsonStore &payload) {
            updateMediaDetails(generation, catalog, payload);
        }};
}

} // namespace xstudio::mikros_media_details
