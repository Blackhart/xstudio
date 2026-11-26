// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/media_details.hpp
 * @brief Media Details screen: playhead media → stamp → CAF worker → DetailJson.
 */
#pragma once

#include "utility/media_meta.hpp"

#include "xstudio/plugin_manager/plugin_base.hpp"
#include "xstudio/utility/media_reference.hpp"

#include <QObject>

#include <caf/message_handler.hpp>

#include <atomic>
#include <cstdint>
#include <string>

namespace xstudio::mikros_media_details {

/**
 * @brief View model of the Media Details panel.
 *
 * Clears QML, bumps generation, reads the Mikros stamp, launches a detached
 * CAF worker for version + parents, and applies the result from the worker mail.
 */
class MediaDetails : public QObject {
    Q_OBJECT

  public:
    /**
     * @brief Publishes the empty detail attributes.
     * @param panel Panel actor that owns the attributes.
     */
    explicit MediaDetails(plugin::StandardPlugin &panel);

    /**
     * @brief Drops the screen. In-flight loads are ignored.
     */
    ~MediaDetails() override;

    /**
     * @brief Handles a panel attribute change owned by this screen.
     *
     * Ignores non-Value roles. ShotgridBaseUrl is preference-backed.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

    /**
     * @brief Reloads the panel from the on-screen media.
     *
     * Clears QML, bumps generation, shows loading, then reads the stamp.
     * Missing media clears the panel. No Mikros stamp shows `not_mikros`.
     * @param media On-screen media actor, or null.
     * @param media_ref Path and frame data for the Media section.
     */
    void on_screen_media_changed(caf::actor media, const utility::MediaReference &media_ref);

    /**
     * @brief Applies a worker or thumb payload when the generation still matches.
     *
     * Called from the panel event handler. Publishes ready/error details, then
     * starts the thumbnail resolve for a successful version payload.
     * @param generation Request id from the worker.
     * @param catalog `version_detail`, `version_detail_error`, or `version_thumb`.
     * @param payload Detail JSON, error message, or `{image}` thumb URL.
     */
    void applyWorkerPage(
        std::uint64_t generation, const std::string &catalog, const utility::JsonStore &payload);

  private:
    /**
     * @brief Spawns the CAF worker that loads version + parents.
     *
     * Does not bump `generation_`: the playhead handler already did that.
     * @param versionId ShotGrid version id from the media stamp.
     * @param mediaDetails Media section already loaded from the playhead.
     * @param generation Request id from `on_screen_media_changed`.
     */
    void loadVersionDetailsFromIdAsync(
        std::int64_t versionId,
        const nlohmann::json &mediaDetails,
        std::uint64_t generation);

    /**
     * @brief Clears DetailJson / DetailState / DetailError. Does not bump generation.
     */
    void clearDetails();

    /**
     * @brief Publishes DetailJson, DetailState, and DetailError together.
     * @param detail Full UI payload, or empty object.
     * @param state `empty`, `not_mikros`, `loading`, `ready`, or `error`.
     * @param error Error text when state is `error`, otherwise empty.
     */
    void sendDetailsToQml(
        const nlohmann::json &detail,
        const std::string &state,
        const std::string &error = {});

    /**
     * @brief Starts cache/download for the version thumb, then mails the panel.
     * @param generation Request id that must still match when the URL arrives.
     * @param projectId Project id for the cache path.
     * @param versionId Version id for the cache path.
     * @param image Remote or local image from the version row.
     * @param imageProvider `http` or `file`.
     */
    void applyThumbnail(
        std::uint64_t generation,
        std::int64_t projectId,
        std::int64_t versionId,
        std::string image,
        std::string imageProvider);

    /**
     * @brief Writes a `file://` thumb URL into the published DetailJson.
     * @param fileUrl Local cache URL from `scheduleOneThumb`.
     */
    void applyThumbUrl(const std::string &fileUrl);

    /**
     * @brief Creates DetailState, DetailError, DetailJson, and ShotgridBaseUrl.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

    /**
     * @brief Applies env-backed defaults after the attributes exist.
     *
     * Call after `initQmlAttributes`. Sets ShotgridBaseUrl from `SHOTGRID_URL`
     * when present.
     */
    void setDefaultValues();

    /**
     * @brief Publishes a not_mikros state with the playhead media section only.
     * @param mediaDetails Fields from `loadMediaDetails`.
     */
    void showNotMikros(const nlohmann::json &mediaDetails);

    plugin::StandardPlugin &panel_; ///< Actor that owns attributes and receives pages.
    module::StringAttribute *detail_state_ = nullptr; ///< empty / not_mikros / loading / ready / error.
    module::StringAttribute *detail_error_ = nullptr; ///< Error text when state is error.
    module::JsonAttribute *detail_json_ = nullptr;    ///< Full UI payload.
    module::StringAttribute *shotgrid_base_url_ = nullptr; ///< Pref for ShotGrid links.
    std::atomic<std::uint64_t> generation_{0}; ///< Bumped on each on_screen_media_changed.
};

} // namespace xstudio::mikros_media_details
