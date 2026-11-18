// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/context/shotbrowser_state.hpp
 * @brief Selected ids, the left tab, and the right tab.
 *
 * `dal.hpp` includes this header so a screen can name the enums.
 * Only `dal.cpp` uses `ShotbrowserState`.
 */
#pragma once

#include <cstdint>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief Left tab of the panel.
 *
 * `Shot` shows the sequence tree. `Asset` shows the asset list.
 */
enum class BrowserMode {
    Shot,  ///< Sequence and shot tree.
    Asset, ///< Asset list.
};

/**
 * @brief Right tab of the panel.
 *
 * `Versions` shows the version list. `Playlist` shows the playlist tree.
 */
enum class ResultView {
    Versions, ///< Version list.
    Playlist, ///< Playlist tree.
};

/**
 * @brief Project, sequence, shot, asset, and the two tabs the panel is showing.
 *
 * `-1` means nothing is selected. Screens ask the DAL for these values.
 * That read does not say a click just happened.
 */
class ShotbrowserState {
  public:
    /**
     * @brief Id of the current project.
     * @return The project id, or `-1` when none is selected.
     */
    std::int64_t selectedProject() const { return selected_project_; }

    /**
     * @brief Remembers the project the user picked.
     * @param projectId Project id, or `-1` when the combo is on None.
     */
    void setSelectedProject(std::int64_t projectId) { selected_project_ = projectId; }

    /**
     * @brief Id of the current sequence.
     * @return The sequence id, or `-1` when none is selected.
     */
    std::int64_t selectedSequence() const { return selected_sequence_; }

    /**
     * @brief Remembers the sequence the user picked.
     * @param sequenceId Sequence id, or `-1` when none is selected.
     */
    void setSelectedSequence(std::int64_t sequenceId) { selected_sequence_ = sequenceId; }

    /**
     * @brief Id of the current shot.
     * @return The shot id, or `-1` when none is selected.
     */
    std::int64_t selectedShot() const { return selected_shot_; }

    /**
     * @brief Remembers the shot the user picked.
     * @param shotId Shot id, or `-1` when none is selected.
     */
    void setSelectedShot(std::int64_t shotId) { selected_shot_ = shotId; }

    /**
     * @brief Id of the current asset.
     * @return The asset id, or `-1` when none is selected.
     */
    std::int64_t selectedAsset() const { return selected_asset_; }

    /**
     * @brief Remembers the asset the user picked.
     * @param assetId Asset id, or `-1` when none is selected.
     */
    void setSelectedAsset(std::int64_t assetId) { selected_asset_ = assetId; }

    /**
     * @brief Left tab of the panel.
     * @return `Shot` or `Asset`.
     */
    BrowserMode browserMode() const { return browser_mode_; }

    /**
     * @brief Remembers the left tab the user picked.
     * @param mode `Shot` or `Asset`.
     */
    void setBrowserMode(BrowserMode mode) { browser_mode_ = mode; }

    /**
     * @brief Right tab of the panel.
     * @return `Versions` or `Playlist`.
     */
    ResultView resultView() const { return result_view_; }

    /**
     * @brief Remembers the right tab the user picked.
     * @param view `Versions` or `Playlist`.
     */
    void setResultView(ResultView view) { result_view_ = view; }

  private:
    std::int64_t selected_project_ = -1;  ///< Current project, or `-1` for None.
    std::int64_t selected_sequence_ = -1; ///< Current sequence, or `-1` when none.
    std::int64_t selected_shot_ = -1;     ///< Current shot, or `-1` when none.
    std::int64_t selected_asset_ = -1;    ///< Current asset, or `-1` when none.
    BrowserMode browser_mode_ = BrowserMode::Shot; ///< Left tab. `Shot` until a preference says otherwise.
    ResultView result_view_ = ResultView::Versions; ///< Right tab. `Versions` until a preference says otherwise.
};

} // namespace xstudio::mikros_shotbrowser
