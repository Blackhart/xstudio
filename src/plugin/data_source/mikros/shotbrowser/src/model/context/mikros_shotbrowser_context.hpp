// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/context/mikros_shotbrowser_context.hpp
 * @brief CAF plugin context: selection state and async DAL loads for the UI.
 */
#pragma once

#include "model/dal/dal.hpp"
#include "model/poco/asset.hpp"
#include "model/poco/playlist.hpp"
#include "model/poco/project.hpp"
#include "model/poco/sequence.hpp"
#include "model/poco/shot.hpp"

#include "xstudio/plugin_manager/plugin_base.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Plugin actor that owns UI attributes, selection state, and async loads.
 *
 * Calls the DAL on worker threads and applies results via CAF messages. Version
 * pages are pushed to the combo as they arrive. Does not include `pms/pms.hpp`
 * and does not sleep in a handler.
 *
 * Cascades after Project (Sequence and Asset are mutually exclusive):
 * - Project → Sequence → Shot → Version
 * - Project → Asset → Version
 *
 * Playlist is a weak optional filter AND-ed with the strong parent when both
 * are set (versions of a shot/sequence/asset that also sit in the playlist).
 * Playlist alone lists every version linked to that playlist.
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
     * @brief Handles async DAL payloads for catalogs and version names.
     * @return Extra CAF handlers merged into the plugin behavior.
     */
    caf::message_handler message_handler_extensions() override;

    /**
     * @brief Reacts to Project / Sequence / Shot / Asset / Playlist combo changes.
     * @param attr_uuid Changed attribute id.
     * @param role Attribute role that changed.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, const int role) override;

  private:
    /** @brief Builds combo labels, appending ` (id)` when names collide. */
    std::vector<std::string> projectLabels() const;
    /** @brief Builds combo labels, appending ` (id)` when names collide. */
    std::vector<std::string> sequenceLabels() const;
    /** @brief Builds combo labels, appending ` (id)` when names collide. */
    std::vector<std::string> shotLabels() const;
    /** @brief Builds combo labels, appending ` (id)` when names collide. */
    std::vector<std::string> assetLabels() const;
    /** @brief Builds combo labels, appending ` (id)` when names collide. */
    std::vector<std::string> playlistLabels() const;

    /** @brief Resolves a Project combo label to its POCO row. */
    std::optional<poco::Project> projectForLabel(const std::string &label) const;
    /** @brief Resolves a Sequence combo label to its POCO row. */
    std::optional<poco::Sequence> sequenceForLabel(const std::string &label) const;
    /** @brief Resolves a Shot combo label to its POCO row. */
    std::optional<poco::Shot> shotForLabel(const std::string &label) const;
    /** @brief Resolves an Asset combo label to its POCO row. */
    std::optional<poco::Asset> assetForLabel(const std::string &label) const;
    /** @brief Resolves a Playlist combo label to its POCO row. */
    std::optional<poco::Playlist> playlistForLabel(const std::string &label) const;

    /** @brief Clears sequence and shot selection and combo values. */
    void clearSequenceBranch();
    /** @brief Clears asset selection and combo value. */
    void clearAssetBranch();

    /** @brief Starts a worker that loads projects via the DAL. */
    void loadProjectsAsync();
    /** @brief Clears then reloads sequences for the selected project. */
    void refreshSequences();
    /** @brief Clears then reloads shots for the selected sequence. */
    void refreshShots();
    /** @brief Clears then reloads assets for the selected project. */
    void refreshAssets();
    /** @brief Clears then reloads playlists for the selected project. */
    void refreshPlaylists();
    /** @brief Clears then reloads version names for the current selection. */
    void refreshVersions();

    std::vector<poco::Project> projects_;     ///< Last project catalog from the DAL.
    std::vector<poco::Sequence> sequences_;   ///< Sequences for `selected_project_`.
    std::vector<poco::Shot> shots_;           ///< Shots for `selected_sequence_`.
    std::vector<poco::Asset> assets_;         ///< Assets for `selected_project_`.
    std::vector<poco::Playlist> playlists_;   ///< Playlists for `selected_project_`.
    std::optional<poco::Project> selected_project_;     ///< Current Project combo selection.
    std::optional<poco::Sequence> selected_sequence_;   ///< Current Sequence combo selection.
    std::optional<poco::Shot> selected_shot_;           ///< Current Shot combo selection.
    std::optional<poco::Asset> selected_asset_;         ///< Current Asset combo selection.
    std::optional<poco::Playlist> selected_playlist_;   ///< Current Playlist combo selection.

    std::uint64_t sequence_request_id_ = 0;  ///< Drops stale sequence worker replies.
    std::uint64_t shot_request_id_ = 0;      ///< Drops stale shot worker replies.
    std::uint64_t asset_request_id_ = 0;     ///< Drops stale asset worker replies.
    std::uint64_t playlist_request_id_ = 0;  ///< Drops stale playlist worker replies.
    std::uint64_t version_request_id_ = 0;   ///< Drops stale version worker replies.

    module::StringChoiceAttribute *project_ = nullptr;   ///< Project combo.
    module::StringChoiceAttribute *sequence_ = nullptr;  ///< Sequence combo.
    module::StringChoiceAttribute *shot_ = nullptr;      ///< Shot combo.
    module::StringChoiceAttribute *asset_ = nullptr;     ///< Asset combo.
    module::StringChoiceAttribute *playlist_ = nullptr;  ///< Playlist combo.
    module::StringChoiceAttribute *version_ = nullptr;   ///< Version combo.
};

} // namespace xstudio::mikros_shotbrowser
