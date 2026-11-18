// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/playlist_tree.hpp
 * @brief Playlist tree: names, and the versions of the playlist the user unfolded.
 */
#pragma once

#include "utility/catalog.hpp"

using namespace xstudio;

#include <QObject>

#include <caf/message_handler.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief View model of the playlist tree.
 *
 * Names use one request id. Versions of an unfolded playlist do not stop.
 * `loadPlaylistListAsync` returns immediately.
 */
class PlaylistTree : public QObject {
    Q_OBJECT

  public:
    /**
     * @brief Publishes the empty playlist tree.
     * @param panel Panel actor. Attributes are created on it, and loads are spawned through it.
     */
    explicit PlaylistTree(plugin::StandardPlugin &panel);

    /**
     * @brief Loads the versions of the playlist the user unfolded.
     *
     * Other attributes are ignored.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value is an unfold.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

    /**
     * @brief Stops the previous name fetch, clears the tree, then loads on the Playlist tab.
     *
     * The request id always moves. The catalog starts only when a project is selected
     * and `view` is Playlist. Returns before the pages arrive.
     * @param view Right tab. Versions leaves the tree empty.
     */
    void loadPlaylistListAsync(ResultView view);

    /**
     * @brief Reloads the versions of the playlist that is already unfolded.
     *
     * Versions does nothing. An empty unfold name does nothing.
     * @param view Right tab.
     */
    void reloadExpanded(ResultView view);

    /**
     * @brief Copies stored thumbnail URLs onto the version rows.
     */
    void stampPlaylistThumbs();

    /**
     * @brief Handles a playlist page and declines every other catalog.
     * @return The playlist-page handler.
     */
    caf::message_handler message_handler_extensions();

  private:
    /**
     * @brief Publishes the playlist tree and the unfolded name.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

    /**
     * @brief Loads the versions nested under one playlist. Does not move the request id.
     * @param playlistId Playlist the user unfolded.
     */
    void loadVersionsForPlaylistAsync(std::int64_t playlistId);

    /**
     * @brief True when that playlist's versions were already loaded to the last page.
     * @param playlistId Playlist in the current tree.
     * @return True when `versionsLoaded` is set on that node.
     */
    bool versionsReady(std::int64_t playlistId) const;

    /**
     * @brief Loads versions for the name written in `ExpandedPlaylist`.
     */
    void expandPlaylist();

    /**
     * @brief Clears the unfolded playlist without notifying the panel.
     */
    void clearExpanded();

    /**
     * @brief Replaces or extends the playlist nodes from one page, then sorts them.
     * @param append False for the first page, which replaces the playlist list.
     * @param payload `{id, name}` rows from the worker.
     */
    void updatePlaylistList(bool append, const utility::JsonStore &payload);

    /**
     * @brief Adds one page of versions into that playlist, then sorts those versions.
     * @param playlistId Playlist the page belongs to.
     * @param append False for the first page, which replaces that playlist's versions.
     * @param payload Version rows from the worker.
     */
    void updateVersionsForPlaylist(
        std::int64_t playlistId, bool append, const utility::JsonStore &payload);

    /**
     * @brief Sorts playlist nodes by name.
     * @param tree Playlist array the QML shows.
     */
    void sortPlaylistsByName(nlohmann::json &tree) const;

    /**
     * @brief Sorts the version rows of one playlist by name.
     * @param node Playlist object already stored in the tree.
     */
    void sortVersionsByName(nlohmann::json &node) const;

    /**
     * @brief Writes the tree to the attribute the QML reads.
     * @param tree Playlist array, names and version rows.
     */
    void sendPlaylistTreeToQml(const nlohmann::json &tree);

    /**
     * @brief Tree currently displayed.
     * @return The JSON array, or an empty array when the attribute is missing.
     */
    nlohmann::json treeValue() const;

    plugin::StandardPlugin &panel_; ///< Panel actor. Attributes and loads go through it.
    RequestGeneration playlist_request_id_ =
        std::make_shared<std::atomic<std::uint64_t>>(0); ///< Drops stale playlist-name fetches and pages.
    std::function<void(std::uint64_t, bool, const utility::JsonStore &)> name_page_; ///< Applies one name page.
    std::function<void(std::int64_t, bool, const utility::JsonStore &)> version_page_; ///< Applies one version page. It does not stop.
    std::map<std::int64_t, std::vector<poco::Version>> versions_by_playlist_; ///< Versions accumulated per playlist.
    module::JsonAttribute *tree_ = nullptr; ///< Playlists with their version rows.
    module::StringAttribute *expanded_ = nullptr; ///< Playlist the user unfolded.
};

} // namespace xstudio::mikros_shotbrowser
