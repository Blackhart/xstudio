// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_list.hpp
 * @brief Version combo and the cards built from the same rows.
 */
#pragma once

#include "utility/catalog.hpp"

using namespace xstudio;

#include <QObject>

#include <caf/message_handler.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Paths that survived the disk check for one version.
 *
 * `frames_spec` is empty when the sequence is not opened. `movie_path` is a local file.
 */
struct OpenReady {
    std::int64_t version_id = 0; ///< ShotGrid version id, for the log line.
    std::uint64_t token = 0;     ///< Matches the QML request that should be selected.
    bool select = false;         ///< True when the new media should become current.
    std::string name;            ///< Media name.
    std::string playlist_uuid;   ///< Playlist actor uuid.
    std::string frames_spec;     ///< Sequence spec accepted by `parse_cli_posix_path`.
    std::string movie_path;      ///< Local movie file. Empty when there is no movie.
};

/**
 * @brief Builds the JSON one version card reads.
 * @param row Version whose display fields are copied.
 * @param label Unique name shown in the title.
 * @param image Thumbnail path already resolved, or empty until the thumb arrives.
 * @return Object with the card keys. Empty strings stay empty so the card shows N/A.
 */
nlohmann::json versionFacts(
    const poco::Version &row, const std::string &label, const std::string &image);

/**
 * @brief View model of the version list, its cards, and the open action.
 *
 * `loadVersionsAsync` returns immediately. The lambda it stores runs on the
 * panel actor when a version page arrives. The cards are the same rows.
 */
class VersionList : public QObject {
    Q_OBJECT

  public:
    /**
     * @brief Publishes the empty version combo and the empty cards.
     * @param panel Panel actor. Attributes are created on it, and loads are spawned through it.
     */
    explicit VersionList(plugin::StandardPlugin &panel);

    /**
     * @brief Stops the previous version fetch, clears the list, then loads when a project is selected.
     *
     * The request id always moves. Parents are the sequence, shot, and asset stored by the DAL.
     * Returns before the pages arrive.
     */
    void loadVersionsAsync();

    /**
     * @brief Writes one card per loaded version, with the thumbnail already on disk.
     */
    void sendVersionCardsToQml();

    /**
     * @brief Handles a `version` page and an `open_media` result, and declines every other catalog.
     * @return The handlers for those two catalogs.
     */
    caf::message_handler message_handler_extensions();

    /**
     * @brief Starts the disk check when the QML writes `OpenVersion`.
     *
     * Other attributes are ignored. An empty value does nothing.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value is a request.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

  private:
    /**
     * @brief Publishes the version combo, the cards, and the open-media attributes.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

    /**
     * @brief True when the panel is still on the parents of a reply.
     * @param hasSequence True when the page was loaded for a sequence.
     * @param sequenceId Sequence id carried by the page.
     * @param hasShot True when the page was loaded for a shot.
     * @param shotId Shot id carried by the page.
     * @param hasAsset True when the page was loaded for an asset.
     * @param assetId Asset id carried by the page.
     * @return False when any of those three has changed.
     */
    bool parentsStillSelected(
        bool hasSequence,
        std::int64_t sequenceId,
        bool hasShot,
        std::int64_t shotId,
        bool hasAsset,
        std::int64_t assetId) const;

    /**
     * @brief Empties the version list and the cards.
     */
    void clearVersionList();

    /**
     * @brief Replaces or extends `versions_` with one page, then publishes the combo and the cards.
     * @param append False for the first page of a request.
     * @param payload Version rows from the worker.
     */
    void updateVersionList(bool append, const utility::JsonStore &payload);

    /**
     * @brief Parses one open request and checks its files off the actor thread.
     * @param text JSON the panel wrote into `OpenVersion`. Empty does nothing.
     */
    void startOpen(const std::string &text);

    /**
     * @brief Adds the checked version to its playlist.
     *
     * When the request asked to select the media, writes `OpenedMedia` so the panel can select it.
     * @param ready Paths and the target playlist.
     */
    void placeOpenedMedia(const OpenReady &ready);

    /**
     * @brief Creates the movie and frame sources and inserts the media into the playlist.
     * @param ready Paths that survived the disk check.
     * @param playlist Playlist actor that receives the media.
     * @param rate Frame rate of that playlist.
     */
    void addOpenedSources(
        const OpenReady &ready, caf::actor playlist, const utility::FrameRate &rate);

    plugin::StandardPlugin &panel_; ///< Panel actor. Attributes and loads go through it.
    std::vector<poco::Version> versions_; ///< Pages received for this list.
    RequestGeneration version_request_id_ =
        std::make_shared<std::atomic<std::uint64_t>>(0); ///< Drops stale version fetches and pages.
    std::function<void(
        std::uint64_t,
        std::int64_t,
        bool,
        std::int64_t,
        bool,
        std::int64_t,
        bool,
        std::int64_t,
        bool,
        const utility::JsonStore &)>
        page_; ///< Applies one page on the panel actor.
    module::StringChoiceAttribute *version_ = nullptr; ///< Labels the combo displays.
    module::BooleanAttribute *versions_loaded_ = nullptr; ///< True once the current request has delivered a page.
    module::JsonAttribute *version_cards_ = nullptr; ///< Visible versions: name and local image.
    module::StringAttribute *open_version_ = nullptr; ///< JSON request from a double-click or the menu.
    module::StringAttribute *opened_media_ = nullptr; ///< JSON result the panel selects when asked.
};

} // namespace xstudio::mikros_shotbrowser
