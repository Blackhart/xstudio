// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/context/mikros_shotbrowser_context.hpp
 * @brief CAF plugin context: selection state and async DAL loads for the UI.
 */
#pragma once

#include "model/dal/dal.hpp"
#include "model/media_open.hpp"
#include "model/poco/asset.hpp"
#include "model/poco/project.hpp"
#include "model/poco/sequence.hpp"
#include "model/poco/shot.hpp"
#include "model/poco/version.hpp"

#include "xstudio/plugin_manager/plugin_base.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Plugin actor that owns UI attributes, selection state, and async loads.
 *
 * Calls the DAL on a detached actor per load and applies results via CAF messages. Catalog
 * and version pages are pushed to the combos as they arrive. Does not include
 * `pms/pms.hpp` and does not sleep in a handler.
 *
 * A project load fills only the open tab, plus the version list. The shot tab
 * loads sequences. The asset tab loads assets. Shots of one sequence are loaded
 * when that sequence is unfolded, and the version list narrows to it. Playlist
 * names load when the playlist view is open. Versions under one playlist load
 * when that playlist is unfolded.
 *
 * Changing a selector clears every child selector, then reloads the version list:
 * - Sequence narrows versions to that sequence and clears Shot.
 * - Shot narrows versions to that shot.
 * - Asset narrows versions to that asset and clears the sequence branch.
 *
 * Sequence and Asset are mutually exclusive. Shots are resolved from the shot
 * tree, not from a separate combo catalog.
 */
class MikrosShotBrowserContext : public plugin::StandardPlugin {
  public:
    inline static const utility::Uuid PLUGIN_UUID =
        utility::Uuid("a022285c-2451-4a29-8535-74c2b63ee0d7");

    /**
     * @brief Builds attributes, connects to the UI, and starts the project load.
     * @param cfg CAF actor config.
     * @param init_settings Plugin init JSON from xSTUDIO.
     */
    MikrosShotBrowserContext(caf::actor_config &cfg, const utility::JsonStore &init_settings);

    ~MikrosShotBrowserContext() override = default;

  protected:
    /**
     * @brief Handles async DAL payloads for catalog pages.
     * @return Extra CAF handlers merged into the plugin behavior.
     */
    caf::message_handler message_handler_extensions() override;

    /**
     * @brief Reacts to Project / Sequence / Shot / Asset value changes.
     * @param attr_uuid Changed attribute id.
     * @param role Attribute role that changed.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, const int role) override;

  private:
    /** @brief Registers the combos and trees the panel binds to. */
    void addPanelAttributes();

    /** @brief Resolves a Project combo label to its POCO row. */
    std::optional<poco::Project> findProjectByLabel(const std::string &label) const;
    /** @brief Resolves a sequence name from the shot tree. */
    std::optional<poco::Sequence> findSequenceByLabel(const std::string &label) const;
    /** @brief Resolves a shot label from the shot tree. */
    std::optional<poco::Shot> findShotByLabel(const std::string &label) const;
    /** @brief Sequence that owns a shot label in the expandable tree. */
    std::optional<poco::Sequence> findSequenceOwningShot(const std::string &shotLabel) const;
    /** @brief Resolves an Asset combo label to its POCO row. */
    std::optional<poco::Asset> findAssetByLabel(const std::string &label) const;

    /** @brief Clears the sequence selection and logs it when one was set. */
    void clearSequenceSelection();
    /** @brief Clears the shot selection and logs it when one was set. */
    void clearShotSelection();
    /** @brief Clears the asset selection and logs it when one was set. */
    void clearAssetSelection();
    /** @brief Clears the sequence and the shot under it. */
    void clearSequenceBranch();

    /** @brief Empties the project combo. The empty value notifies and clears the children. */
    void resetProjectCatalog();
    /** @brief Clears then streams project pages via the DAL. */
    void loadProjectsAsync();
    /** @brief Streams project pages for one request id. */
    void startProjectLoad(std::uint64_t requestId);

    /** @brief Clears the shot tree and streams sequences, without their shots. */
    void refreshShotTree();
    /** @brief Streams sequence rows for one project on a detached actor. */
    void startShotTreeLoad(std::uint64_t requestId, std::int64_t projectId);
    /**
     * @brief Streams the shots of one sequence into its tree node.
     * @param requestId Shot-tree generation. A newer tree drops these pages.
     * @param projectId Selected project id.
     * @param sequenceId Sequence whose shots the user unfolded.
     */
    void startSequenceShotLoad(
        std::uint64_t requestId, std::int64_t projectId, std::int64_t sequenceId);
    /** @brief True when that sequence's shots were already requested to completion. */
    bool sequenceShotsReady(std::int64_t sequenceId) const;

    /** @brief Empties the asset combo. The empty value notifies. */
    void resetAssetCatalog();
    /** @brief Clears then streams asset pages for the selected project. */
    void refreshAssets();
    /** @brief Streams asset pages for one project. */
    void startAssetLoad(std::uint64_t requestId, std::int64_t projectId);

    /** @brief Empties the version combo and marks the list as still loading. */
    void resetVersionCatalog();
    /** @brief Clears then streams version pages for the current selection. */
    void refreshVersions();
    /**
     * @brief Streams version pages for the current sequence, shot, or asset.
     * @param requestId Request that must still be current when a page arrives.
     * @param projectId Selected project id.
     */
    void startVersionLoad(std::uint64_t requestId, std::int64_t projectId);

    /** @brief Clears playlists and streams their names, without their versions. */
    void refreshPlaylistTree();
    /**
     * @brief Streams the versions nested under one playlist.
     * @param requestId Playlist-tree generation. A newer tree drops these pages.
     * @param projectId Selected project id.
     * @param playlistId Playlist the user unfolded.
     */
    void startPlaylistVersionLoad(
        std::uint64_t requestId, std::int64_t projectId, std::int64_t playlistId);
    /** @brief True when that playlist's versions were already requested to completion. */
    bool playlistVersionsReady(std::int64_t playlistId) const;
    /**
     * @brief Builds the playlist tree for one project on a detached actor.
     * @param requestId Request that must still be current when the tree arrives.
     * @param projectId Selected project id.
     */
    void startPlaylistLoad(std::uint64_t requestId, std::int64_t projectId);

    /** @brief Loads the open tab and the version list for the chosen project. */
    void onProjectChosen();
    /** @brief Loads sequences or assets when the user changes tab. */
    void onBrowserModeChosen();
    /** @brief Loads playlist names when the user opens the playlist view. */
    void onResultViewChosen();
    /** @brief Loads one sequence's shots and narrows versions to that sequence. */
    void onSequenceExpanded();
    /** @brief Loads one playlist's versions. */
    void onPlaylistExpanded();
    /** @brief Reloads the unfolded playlist after the version filter changes. */
    void reloadExpandedPlaylist();
    /** @brief Drops asset rows without asking the backend. */
    void clearAssetCatalog();
    /** @brief Drops the shot tree without asking the backend. */
    void clearShotTree();
    /** @brief Applies a new sequence and narrows the versions. */
    void onSequenceChosen();
    /** @brief Applies a new shot, aligns its sequence, and narrows the versions. */
    void onShotChosen();
    /** @brief Sets the sequence that owns the shot, without clearing that shot. */
    void alignSequenceWithShot(const std::string &shotLabel);
    /** @brief Applies a new asset and narrows the versions. */
    void onAssetChosen();

    /**
     * @brief True when `projectId` is still the selected project.
     * @param projectId Project id carried by a worker reply.
     */
    bool projectStillSelected(std::int64_t projectId) const;
    /**
     * @brief True when the sequence, shot, and asset on a reply are still selected.
     */
    bool versionParentsStillSelected(
        bool hasSequence,
        std::int64_t sequenceId,
        bool hasShot,
        std::int64_t shotId,
        bool hasAsset,
        std::int64_t assetId) const;

    /** @brief Applies one project page when the request is still current. */
    void applyProjectPage(std::uint64_t requestId, bool append, const utility::JsonStore &payload);
    /** @brief Applies a shot tree when the request is still current. */
    void applyShotTree(std::uint64_t requestId, const utility::JsonStore &payload);
    /** @brief Writes one sequence's shot page into the tree. */
    void applySequenceShots(std::uint64_t requestId, const utility::JsonStore &payload);
    /** @brief Writes one playlist's version page into the tree. */
    void applyPlaylistVersions(std::uint64_t requestId, const utility::JsonStore &payload);
    /** @brief Applies one asset page when the request is still current. */
    void applyAssetPage(std::uint64_t requestId, bool append, const utility::JsonStore &payload);
    /** @brief Applies a playlist tree when the request and parents still match. */
    void applyPlaylistTree(
        std::uint64_t requestId,
        bool hasSequence,
        std::int64_t sequenceId,
        bool hasShot,
        std::int64_t shotId,
        bool hasAsset,
        std::int64_t assetId,
        const utility::JsonStore &payload);
    /** @brief Stores file URLs and refreshes cards still on screen. */
    void rememberThumbs(std::int64_t projectId, const utility::JsonStore &payload);
    /** @brief Publishes name and local image for each visible version. */
    void publishVersionCards();
    /** @brief Copies stored file URLs onto playlist version rows. */
    void stampPlaylistThumbs();
    /**
     * @brief File URL already stored for one version.
     * @param projectId Numeric project id.
     * @param versionId Numeric version id.
     * @return `file://` URL, or empty when the image is not ready.
     */
    std::string thumbFor(std::int64_t projectId, std::int64_t versionId) const;
    /** @brief Applies one version page when the request and parents still match. */
    void applyVersionPage(
        std::uint64_t requestId,
        bool hasSequence,
        std::int64_t sequenceId,
        bool hasShot,
        std::int64_t shotId,
        bool hasAsset,
        std::int64_t assetId,
        bool append,
        const utility::JsonStore &payload);

    std::vector<poco::Project> projects_;   ///< Project pages received so far.
    std::vector<poco::Asset> assets_;       ///< Asset pages for `selected_project_`.
    std::vector<poco::Version> versions_;   ///< Version pages for the current selection.
    std::optional<poco::Project> selected_project_;   ///< Current Project combo selection.
    std::optional<poco::Sequence> selected_sequence_; ///< Sequence selected in the shot tree.
    std::optional<poco::Shot> selected_shot_;         ///< Shot selected in the shot tree.
    std::optional<poco::Asset> selected_asset_;       ///< Current Asset selection.

    std::uint64_t project_request_id_ = 0;   ///< Drops stale project worker replies.
    std::uint64_t sequence_request_id_ = 0;  ///< Drops stale shot-tree worker replies.
    std::uint64_t asset_request_id_ = 0;     ///< Drops stale asset worker replies.
    std::uint64_t version_request_id_ = 0;   ///< Drops stale version worker replies.
    std::uint64_t playlist_request_id_ = 0;  ///< Drops stale playlist-tree worker replies.
    bool aligning_sequence_ = false;         ///< Shot selection updates its sequence without clearing the shot.
    bool sequences_ready_ = false;           ///< Sequences were requested for the current project.
    bool assets_ready_ = false;              ///< Assets were requested for the current project.
    bool playlists_ready_ = false;           ///< Playlist names were requested for the current project.

    module::StringAttribute *browser_mode_ = nullptr;       ///< `shot` or `asset`.
    module::StringAttribute *result_view_ = nullptr;        ///< `versions` or `playlist`.
    module::StringAttribute *expanded_sequence_ = nullptr;  ///< Sequence the user unfolded.
    module::StringAttribute *expanded_playlist_ = nullptr;  ///< Playlist the user unfolded.

    module::StringChoiceAttribute *project_ = nullptr;   ///< Project combo.
    module::StringChoiceAttribute *sequence_ = nullptr;  ///< Sequence combo.
    module::StringChoiceAttribute *shot_ = nullptr;      ///< Shot combo.
    module::JsonAttribute *shot_tree_ = nullptr;         ///< Sequences with their shot names.
    module::StringChoiceAttribute *asset_ = nullptr;     ///< Asset combo.
    module::StringChoiceAttribute *version_ = nullptr;   ///< Version combo.
    module::BooleanAttribute *versions_loaded_ = nullptr; ///< True once the current version request has delivered a page.
    module::JsonAttribute *version_cards_ = nullptr;     ///< Visible versions: name and local image.
    module::JsonAttribute *playlist_tree_ = nullptr;     ///< Playlists with their version rows.
    module::StringAttribute *open_version_ = nullptr;   ///< JSON request from a double-click or the menu.
    module::StringAttribute *opened_media_ = nullptr;   ///< JSON result the panel selects when asked.
    std::map<std::pair<std::int64_t, std::int64_t>, std::string> thumb_paths_; ///< `(projectId, versionId)` to `file://`.

    /** @brief Adds one resolved version to its playlist. */
    void placeOpenedMedia(const OpenReady &ready);

    /** @brief Spawns the sources and inserts the media. */
    void addOpenedSources(
        const OpenReady &ready, caf::actor playlist, const utility::FrameRate &rate);

    /** @brief Reads one open request and starts the disk check. */
    void startOpen(const std::string &json);
};

} // namespace xstudio::mikros_shotbrowser
