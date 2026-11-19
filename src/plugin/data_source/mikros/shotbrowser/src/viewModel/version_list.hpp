// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_list.hpp
 * @brief Version list view model: catalog load, JSON cards, and open media.
 */
#pragma once

#include "utility/catalog.hpp"
#include "viewModel/version_card.hpp"

using namespace xstudio;

#include <QObject>

#include <caf/message_handler.hpp>

#include <atomic>
#include <cstdint>
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
 * @brief View model of the version catalog load and open action.
 *
 * Holds plain `VersionCard` objects. The list is published as a `JsonAttribute`
 * that QML reads as `VersionCards`.
 */
class VersionList : public QObject {
    Q_OBJECT

  public:
    /**
     * @brief Publishes the empty version cards and the open-media attributes.
     * @param panel Panel actor. Attributes are created on it, and loads are spawned through it.
     */
    explicit VersionList(plugin::StandardPlugin &panel);

    /**
     * @brief Loads when a project is selected.
     *
     * Does not clear the list; the panel calls `clearVersions` first when needed.
     * The request id always moves. Parents are the sequence, shot, and asset stored by the DAL.
     * Returns before the pages arrive.
     */
    void loadVersionsAsync();

    /**
     * @brief Clears the cards and the JSON attribute.
     *
     * The panel calls this before a reload, when the project changes, or when the playlist tab is showing.
     */
    void clearVersions();

    /**
     * @brief Handles `version`, `version_count`, `version_thumbs`, and `open_media`.
     * @return The handlers for those catalogs.
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
    friend class VersionPageHandler;
    friend class VersionThumbHandler;
    friend class OpenPageHandler;

    /**
     * @brief Applies local thumb URLs on matching cards and republishes JSON once.
     * @param payload Array of `{versionId, image}` from the `version_thumbs` mail.
     */
    void applyCardImages(const utility::JsonStore &payload);

    /**
     * @brief Publishes the version cards and the open-media attributes.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

    /**
     * @brief Drops a stale page, otherwise merges new versions by id and publishes JSON.
     * @param requestId Generation carried by the catalog mail.
     * @param append Unused; merge by id is the update path. Kept for the catalog mail shape.
     * @param payload Version rows from the worker.
     */
    void updateVersionList(
        std::uint64_t requestId, bool append, const utility::JsonStore &payload);

    /**
     * @brief Places media after the open-disk check finishes (double-click / menu).
     * @param payload Paths checked off the actor thread.
     */
    void openMedia(const utility::JsonStore &payload);

    /**
     * @brief Keeps only rows whose version id is not already in `versions_`.
     * @param rows Page rows from the worker.
     * @return Rows that still need a card.
     */
    [[nodiscard]] std::vector<poco::VersionListElement>
    findUnknownVersionRows(const std::vector<poco::VersionListElement> &rows) const;

    /**
     * @brief Builds a card for each new row and schedules a batch thumb load.
     * @param newRows Rows that are not yet in `versions_`.
     */
    void appendVersions(const std::vector<poco::VersionListElement> &newRows);

    /**
     * @brief Writes the cards array to the `VersionCards` attribute.
     */
    void sendVersionCardsToQml();

    /**
     * @brief Resolves thumbnails for the given version ids and mails ready URLs.
     * @param projectId Project for the thumb cache path.
     * @param versionIds Ids of the new cards.
     */
    void scheduleVersionThumbs(std::int64_t projectId, std::vector<std::int64_t> versionIds);

    /**
     * @brief Drops a stale count, otherwise writes the version total into the footer.
     * @param requestId Generation carried by the catalog mail.
     * @param payload JSON with a `versions` integer.
     */
    void updateVersionFooter(std::uint64_t requestId, const utility::JsonStore &payload);

    /**
     * @brief Clears the footer attribute text.
     */
    void clearFooterValue();

    /**
     * @brief Sets `VersionsLoaded` to false (loading / cleared).
     */
    void clearVersionsLoadedFlag();

    /**
     * @brief Sets `VersionsLoaded` to true (page applied, including zero rows).
     */
    void markVersionsLoaded();

    void startOpen(const std::string &text);
    void placeOpenedMedia(const OpenReady &ready);
    void addOpenedSources(
        const OpenReady &ready, caf::actor playlist, const utility::FrameRate &rate);

    plugin::StandardPlugin &panel_; ///< Panel actor. Attributes and loads go through it.
    std::vector<std::unique_ptr<VersionCard>> versions_; ///< Cards published as JSON.
    module::JsonAttribute *version_cards_ = nullptr; ///< Array of card objects for QML.
    RequestGeneration version_request_id_ =
        std::make_shared<std::atomic<std::uint64_t>>(0); ///< Drops stale version fetches and pages.
    module::StringAttribute *footer_ = nullptr; ///< Footer text for the QML list.
    module::BooleanAttribute *versions_loaded_ = nullptr; ///< True once the current request has delivered a page.
    module::StringAttribute *open_version_ = nullptr; ///< JSON request from a double-click or the menu.
    module::StringAttribute *opened_media_ = nullptr; ///< JSON result the panel selects when asked.
};

} // namespace xstudio::mikros_shotbrowser
