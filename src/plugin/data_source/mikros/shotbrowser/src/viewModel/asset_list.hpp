// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/asset_list.hpp
 * @brief Asset list: first page, load-more, and selected id signal.
 */
#pragma once

#include "utility/catalog.hpp"

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
 * @brief View model of the asset list.
 *
 * The list shows names and writes the chosen name. This class stores the id
 * through the DAL and emits `onAssetSelected`. `loadAssetsAsync` and
 * `loadMoreAssetsAsync` return before the pages arrive.
 */
class AssetList : public QObject {
    Q_OBJECT

  public:
    /**
     * @brief Publishes the empty asset list and the load-more attribute.
     * @param panel Panel actor. Attributes are created on it, and loads are spawned through it.
     */
    explicit AssetList(plugin::StandardPlugin &panel);

    /**
     * @brief Starts a load-more or stores a new selected id.
     *
     * `LoadMoreAssets` starts the next page. The asset combo value stores the id
     * and emits `onAssetSelected`. Other attributes are ignored. Publishing the
     * asset list does not emit when the id is already the one stored.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value is a request.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

    /**
     * @brief Loads the first asset page when `assets_` is empty.
     *
     * Rows already on screen are kept. Resets the continuation cursor. The catalog
     * starts only when a project is selected and the left tab is Asset. Returns
     * before the page arrives.
     */
    void loadAssetsAsync();

    /**
     * @brief Loads the next asset page when the list still has a continuation cursor.
     *
     * No-op when there is no cursor, a more-page is already incoming, or no project is
     * selected. Keeps the current request id. Returns before the page arrives.
     */
    void loadMoreAssetsAsync();

    /**
     * @brief Clears `assets_` and stops a load that is still running.
     *
     * Resets the continuation cursor. The panel calls this when the selected project
     * changes. Does not clear the selected asset.
     */
    void clearAssetsTree();

    /**
     * @brief Clears the selected asset.
     *
     * The panel calls this when a sequence or shot is selected.
     */
    void clearSelectedAsset();

    /**
     * @brief Stores the asset id and emits `onAssetSelected`.
     * @param assetId Asset id, or `-1` when none is selected.
     */
    void setSelectedAsset(std::int64_t assetId);

    /**
     * @brief Handles an `asset` page or `asset_count` and declines every other catalog.
     * @return The asset-page handler.
     */
    caf::message_handler message_handler_extensions();

  signals:
    /**
     * @brief Emitted after the selected id is stored.
     * @param assetId Asset id, or `-1` when the combo is cleared by the user.
     */
    void onAssetSelected(std::int64_t assetId);

  private:
    /**
     * @brief Publishes the asset list and load-more attributes.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

    /**
     * @brief Reads the label written by the combo and stores its id.
     */
    void applyAssetFromQml();

    /**
     * @brief True when a project id is stored.
     * @return True when `dal::selectedProject` is not `-1`.
     */
    bool checkProjectSelected() const;

    /**
     * @brief True when the left tab is the asset list.
     * @return True when `dal::browserMode` is `Asset`.
     */
    bool checkBrowserModeIsAsset() const;

    /**
     * @brief Replaces or extends `assets_` with one page, then publishes it.
     * @param append False for the first page of a request.
     * @param payload Object with `rows` and optional `next_cursor` from the worker.
     */
    void updateAssetList(bool append, const utility::JsonStore &payload);

    /**
     * @brief Stores the asset total and refreshes the footer.
     * @param payload JSON with an `assets` integer.
     */
    void updateAssetFooter(const utility::JsonStore &payload);

    /**
     * @brief Writes `loaded / total assets` into the footer.
     */
    void refreshFooterValue();

    /**
     * @brief Clears the footer attribute text and the stored total.
     */
    void clearFooterValue();

    /**
     * @brief Sets `AssetsLoaded` to false (loading / cleared).
     */
    void clearAssetsLoadedFlag();

    /**
     * @brief Sets `AssetsLoaded` to true (page applied, including zero rows).
     */
    void markAssetsLoaded();

    /**
     * @brief Writes the asset labels to the attribute the combo displays.
     */
    void sendAssetsListToQml();

    plugin::StandardPlugin &panel_; ///< Panel actor. Attributes and loads go through it.
    std::vector<poco::Asset> assets_; ///< Pages received for this list.
    RequestGeneration asset_request_id_ =
        std::make_shared<std::atomic<std::uint64_t>>(0); ///< Drops stale asset fetches and pages.
    std::optional<std::string> next_cursor_; ///< Continuation for the next page. Empty when finished.
    bool more_incoming_ = false; ///< True while a load-more page is still on the way.
    int asset_total_ = -1; ///< Server match count for the footer. -1 until the count arrives.
    module::StringChoiceAttribute *asset_ = nullptr; ///< Labels the list displays.
    module::StringAttribute *footer_ = nullptr; ///< Footer text for the QML list.
    module::BooleanAttribute *assets_loaded_ = nullptr; ///< True once the current request has delivered a page.
    module::StringAttribute *load_more_assets_ = nullptr; ///< QML bump that requests the next page.
};

} // namespace xstudio::mikros_shotbrowser
