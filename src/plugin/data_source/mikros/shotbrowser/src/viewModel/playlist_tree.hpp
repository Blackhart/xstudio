// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/playlist_tree.hpp
 * @brief Playlist tree: paged names, and the versions of the playlist the user unfolded.
 */
#pragma once

#include "utility/catalog.hpp"

using namespace xstudio;

#include <QObject>

#include <caf/message_handler.hpp>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief View model of the playlist tree.
 *
 * Names use one request id and continue with `loadMorePlaylistsAsync`. Versions
 * of an unfolded playlist do not stop. `loadPlaylistListAsync` returns immediately.
 * Pages are applied in `message_handler_extensions`.
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
     * @brief Loads more playlist names, expands a playlist, or runs a search.
     *
     * `LoadMorePlaylists` starts the next name page. `ExpandedPlaylist` starts a version load.
     * `PlaylistSearch` clears then reloads with the name-or-id filter.
     * Other attributes are ignored.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value is a request.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

    /**
     * @brief Loads the first name page when Playlist tab and project are set.
     *
     * Does not clear the tree; the panel calls `clearPlaylists` first when needed.
     * The request id always moves. Applies `search_query_` when set. Reads `dal::resultView`.
     * Returns before the pages arrive.
     */
    void loadPlaylistListAsync();

    /**
     * @brief Stores a name-or-id filter and loads the first matching name page.
     *
     * Empty text clears the filter and loads the unfiltered list. Does not clear the
     * tree; the caller runs `clearPlaylists` first. Returns before the pages arrive.
     * @param text Exact id (all digits) or name substring. Trimmed by the DAL.
     */
    void loadPlaylistsFromNameOrIdAsync(const std::string &text);

    /**
     * @brief Loads the next playlist-name page when a continuation cursor remains.
     *
     * No-op when there is no cursor, a more-page is already incoming, or no project is
     * selected. Keeps the current request id and the active search filter. Returns before
     * the page arrives.
     */
    void loadMorePlaylistsAsync();

    /**
     * @brief Clears the playlist tree and stops a name fetch that is still running.
     *
     * The panel calls this before a reload, when the project changes, or when the versions
     * tab is showing. Does not clear `search_query_`.
     */
    void clearPlaylists();

    /**
     * @brief Reloads the versions of the playlist that is already unfolded.
     *
     * Reads `dal::resultView`. Versions does nothing. An empty unfold name does nothing.
     */
    void reloadExpanded();

    /**
     * @brief Copies stored thumbnail URLs onto the version rows.
     */
    void stampPlaylistThumbs();

    /**
     * @brief Downloads missing thumbs for one playlist page (by version id).
     * @param rows Versions that just arrived for an unfolded playlist.
     */
    void schedulePlaylistThumbs(const std::vector<poco::VersionListElement> &rows);

    /**
     * @brief Handles `playlist_tree`, `playlist_count`, `playlist_versions`, and thumbs.
     * @return The playlist catalog handler.
     */
    caf::message_handler message_handler_extensions();

  private:
    /**
     * @brief Publishes the playlist tree and the unfolded name.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

    /**
     * @brief True when the right tab is the playlist tree.
     * @return True when `dal::resultView` is `Playlist`.
     */
    bool checkResultViewIsPlaylist() const;

    /**
     * @brief Sets `PlaylistsLoaded` to false (loading / cleared).
     */
    void clearPlaylistsLoadedFlag();

    /**
     * @brief Sets `PlaylistsLoaded` to true (page applied, including zero rows).
     */
    void markPlaylistsLoaded();

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
     * @param requestId Generation carried by the catalog mail.
     * @param append False for the first page, which replaces the playlist list.
     * @param payload Object with `rows` and optional `next_cursor` from the worker.
     */
    void updatePlaylistList(
        std::uint64_t requestId, bool append, const utility::JsonStore &payload);

    /**
     * @brief Drops a stale count, otherwise stores the total and refreshes the footer.
     * @param requestId Generation carried by the catalog mail.
     * @param payload JSON with a `playlists` integer.
     */
    void updatePlaylistFooter(std::uint64_t requestId, const utility::JsonStore &payload);

    /**
     * @brief Writes `loaded / total playlists` into the footer.
     */
    void refreshFooterValue();

    /**
     * @brief Clears the footer attribute text and the stored total.
     */
    void clearFooterValue();

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

    /**
     * @brief How many playlist nodes are currently in the tree.
     * @return Node count.
     */
    std::size_t playlistNodeCount() const;

    /**
     * @brief Active search text for catalog loads, or empty when unfiltered.
     * @return `search_query_` when set, otherwise nullopt.
     */
    [[nodiscard]] std::optional<std::string> activeSearchQuery() const;

    plugin::StandardPlugin &panel_; ///< Panel actor. Attributes and loads go through it.
    RequestGeneration playlist_request_id_ =
        std::make_shared<std::atomic<std::uint64_t>>(0); ///< Drops stale playlist-name fetches and pages.
    std::optional<std::string> next_cursor_; ///< Continuation for the next name page. Empty when finished.
    bool more_incoming_ = false; ///< True while a load-more name page is still on the way.
    std::string search_query_; ///< Name-or-id filter. Empty means no Where on name/id.
    int playlist_total_ = -1; ///< Server match count for the footer. -1 until the count arrives.
    std::map<std::int64_t, std::vector<poco::VersionListElement>>
        versions_by_playlist_; ///< List elements accumulated per playlist.
    module::JsonAttribute *tree_ = nullptr; ///< Playlists with their version rows.
    module::StringAttribute *expanded_ = nullptr; ///< Playlist the user unfolded.
    module::BooleanAttribute *playlists_loaded_ = nullptr; ///< True once the current request has delivered a page.
    module::StringAttribute *load_more_playlists_ = nullptr; ///< QML bump that requests the next name page.
    module::StringAttribute *playlist_search_ = nullptr; ///< QML Enter on the result search bar.
    module::StringAttribute *footer_ = nullptr; ///< Footer text for the QML list.
};

} // namespace xstudio::mikros_shotbrowser
