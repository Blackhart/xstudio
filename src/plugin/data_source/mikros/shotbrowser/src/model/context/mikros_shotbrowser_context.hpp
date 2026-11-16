// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/context/mikros_shotbrowser_context.hpp
 * @brief CAF plugin context: selection state and async DAL loads for the UI.
 */
#pragma once

#include "model/dal/dal.hpp"
#include "model/poco/asset.hpp"
#include "model/poco/project.hpp"
#include "model/poco/sequence.hpp"
#include "model/poco/shot.hpp"
#include "model/poco/version.hpp"

#include "xstudio/plugin_manager/plugin_base.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Plugin actor that owns UI attributes, selection state, and async loads.
 *
 * Calls the DAL on worker threads and applies results via CAF messages. Catalog
 * and version pages are pushed to the combos as they arrive. Does not include
 * `pms/pms.hpp` and does not sleep in a handler.
 *
 * Two cascades. Changing a selector clears every child selector to none,
 * then reloads the lists that remain:
 * - Project → Sequence → Shot → Version
 *   Project lists the shot tree and the versions of that project.
 *   Sequence narrows versions to that sequence and clears Shot.
 *   Shot narrows versions to that shot and clears Version.
 * - Project → Asset → Version
 *   Project lists assets and versions of that project.
 *   Asset narrows versions to that asset and clears Version.
 *
 * Sequence and Asset are mutually exclusive. Shots and sequences are resolved
 * from the shot tree, not from a separate combo catalog.
 *
 * Playlists belong to the project. The versions nested under each playlist use
 * the same sequence, shot, or asset filter as the version list, plus that playlist.
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
    /** @brief Resolves a Project combo label to its POCO row. */
    std::optional<poco::Project> projectForLabel(const std::string &label) const;
    /** @brief Resolves a sequence name from the shot tree. */
    std::optional<poco::Sequence> sequenceForLabel(const std::string &label) const;
    /** @brief Resolves a shot label from the shot tree. */
    std::optional<poco::Shot> shotForLabel(const std::string &label) const;
    /** @brief Finds a shot id stored on the shot tree. */
    std::optional<poco::Shot> shotFromTree(const std::string &label) const;
    /** @brief Sequence that owns a shot label in the expandable tree. */
    std::optional<poco::Sequence> sequenceForShotLabel(const std::string &label) const;
    /** @brief Resolves an Asset combo label to its POCO row. */
    std::optional<poco::Asset> assetForLabel(const std::string &label) const;

    /** @brief Clears sequence and shot selection. */
    void clearSequenceBranch();
    /** @brief Clears asset selection and combo value. */
    void clearAssetBranch();

    /** @brief Clears then streams project pages via the DAL. */
    void loadProjectsAsync();
    /** @brief Clears then streams the expandable shot tree for the project. */
    void refreshSequences();
    /** @brief Clears the shot selection without reloading a shot catalog. */
    void clearShotSelection();
    /** @brief Clears then streams asset pages for the selected project. */
    void refreshAssets();
    /** @brief Clears then streams version pages for the current selection. */
    void refreshVersions();
    /**
     * @brief Rebuilds playlists for the project, with versions matching the
     * current sequence, shot, or asset selection.
     */
    void refreshPlaylistTree();

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

    module::StringChoiceAttribute *project_ = nullptr;   ///< Project combo.
    module::StringChoiceAttribute *sequence_ = nullptr;  ///< Sequence combo.
    module::StringChoiceAttribute *shot_ = nullptr;      ///< Shot combo.
    module::JsonAttribute *shot_tree_ = nullptr;         ///< Sequences with their shot names.
    module::StringChoiceAttribute *asset_ = nullptr;     ///< Asset combo.
    module::StringChoiceAttribute *version_ = nullptr;   ///< Version combo.
    module::JsonAttribute *playlist_tree_ = nullptr;     ///< Playlists with their version names.
};

} // namespace xstudio::mikros_shotbrowser
