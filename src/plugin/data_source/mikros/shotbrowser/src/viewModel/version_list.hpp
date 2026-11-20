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
#include <optional>
#include <string>
#include <vector>

namespace xstudio::mikros_shotbrowser {

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
     * @brief Loads the first version page when a project is selected.
     *
     * Does not clear the list; the panel calls `clearVersions` first when needed.
     * The request id always moves. Resets the continuation cursor. Parents are the
     * sequence, shot, and asset stored by the DAL. Applies `search_query_` when set.
     * Returns before the pages arrive.
     */
    void loadVersionsAsync();

    /**
     * @brief Stores a name-or-id filter and loads the first matching page.
     *
     * Empty text clears the filter and loads the unfiltered list. Does not clear the
     * cards; the caller runs `clearVersions` first. Returns before the pages arrive.
     * @param text Exact id (all digits) or name substring. Trimmed by the DAL.
     */
    void loadVersionsFromNameOrIdAsync(const std::string &text);

    /**
     * @brief Loads the next version page when the list still has a continuation cursor.
     *
     * No-op when there is no cursor, a more-page is already incoming, or no project is
     * selected. Keeps the current request id and the active search filter. Returns before
     * the page arrives.
     */
    void loadMoreVersionsAsync();

    /**
     * @brief Clears the cards and the JSON attribute.
     *
     * The panel calls this before a reload, when the project changes, or when the playlist tab is showing.
     * Does not clear `search_query_`.
     */
    void clearVersions();

    /**
     * @brief Handles `version`, `version_count`, `version_thumbs`, and `version_transcoded`.
     * @return The handlers for those catalogs.
     */
    caf::message_handler message_handler_extensions();

    /**
     * @brief Starts a load-more, a search, or an open when QML writes the matching attribute.
     *
     * `LoadMoreVersions` starts the next page. `VersionSearch` clears then reloads
     * with the name-or-id filter. `OpenVersion` calls `open_version`.
     * `PreviewVersion` calls `preview_version`. Other attributes are ignored.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value is a request.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

  private:
    friend class VersionPageHandler;
    friend class VersionThumbHandler;
    friend class VersionTranscodedHandler;

    /**
     * @brief Parses a QML JSON open request, merges card paths, and calls `open_version`.
     * @param text Attribute value: versionId, name, playlistName, frameRange.
     */
    void open_version(const std::string &text);

    /**
     * @brief Parses a QML JSON preview request, merges card paths, and calls `preview_version`.
     * @param text Attribute value: versionId, name, frameRange.
     */
    void preview_version(const std::string &text);

    /**
     * @brief Applies local thumb URLs on matching cards and republishes JSON once.
     * @param payload Array of `{versionId, image}` from the `version_thumbs` mail.
     */
    void applyCardImages(const utility::JsonStore &payload);

    /**
     * @brief Applies signed uploaded-movie URLs on matching cards and republishes JSON once.
     * @param payload Array of `{versionId, uploadedMovie}` from the `version_transcoded` mail.
     */
    void applyCardTranscoded(const utility::JsonStore &payload);

    /**
     * @brief Publishes the version cards and list attributes.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

    /**
     * @brief Drops a stale page, otherwise merges new versions by id and publishes JSON.
     * @param requestId Generation carried by the catalog mail.
     * @param append Unused; merge by id is the update path. Kept for the catalog mail shape.
     * @param payload Object with `rows` and optional `next_cursor` from the worker.
     */
    void updateVersionList(
        std::uint64_t requestId, bool append, const utility::JsonStore &payload);

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
     * @brief Checks local paths then batches signed URLs for rows that still need them.
     * @param projectId Project for the movie cache path.
     * @param rows New list rows that just became cards.
     */
    void scheduleVersionTranscoded(
        std::int64_t projectId, const std::vector<poco::VersionListElement> &rows);

    /**
     * @brief Finds the card for `versionId`, or null when missing.
     * @param versionId Domain version id.
     * @return Matching card, or nullptr.
     */
    [[nodiscard]] VersionCard *findCard(std::int64_t versionId) const;

    /**
     * @brief Drops a stale count, otherwise stores the total and refreshes the footer.
     * @param requestId Generation carried by the catalog mail.
     * @param payload JSON with a `versions` integer.
     */
    void updateVersionFooter(std::uint64_t requestId, const utility::JsonStore &payload);

    /**
     * @brief Writes `loaded / total versions` into the footer (debug load progress).
     */
    void refreshFooterValue();

    /**
     * @brief Clears the footer attribute text and the stored total.
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

    /**
     * @brief Active search text for catalog loads, or empty when unfiltered.
     * @return `search_query_` when set, otherwise nullopt.
     */
    [[nodiscard]] std::optional<std::string> activeSearchQuery() const;

    plugin::StandardPlugin &panel_; ///< Panel actor. Attributes and loads go through it.
    std::vector<std::unique_ptr<VersionCard>> versions_; ///< Cards published as JSON.
    module::JsonAttribute *version_cards_ = nullptr; ///< Array of card objects for QML.
    RequestGeneration version_request_id_ =
        std::make_shared<std::atomic<std::uint64_t>>(0); ///< Drops stale version fetches and pages.
    std::optional<std::string> next_cursor_; ///< Continuation for the next page. Empty when finished.
    bool more_incoming_ = false; ///< True while a load-more page is still on the way.
    std::string search_query_; ///< Name-or-id filter. Empty means no Where on name/id.
    module::StringAttribute *footer_ = nullptr; ///< Footer text for the QML list.
    int version_total_ = -1; ///< Server match count for the footer. -1 until the count arrives.
    module::BooleanAttribute *versions_loaded_ = nullptr; ///< True once the current request has delivered a page.
    module::StringAttribute *load_more_versions_ = nullptr; ///< QML bump that requests the next page.
    module::StringAttribute *version_search_ = nullptr; ///< QML Enter on the result search bar.
    module::StringAttribute *open_version_ = nullptr; ///< JSON request from a double-click or the menu.
    module::StringAttribute *preview_version_ = nullptr; ///< JSON request from a single-click preview.
};

} // namespace xstudio::mikros_shotbrowser
