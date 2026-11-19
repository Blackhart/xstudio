// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/asset_list.hpp
 * @brief Asset combo: publishes labels, and signals the chosen id.
 */
#pragma once

#include "utility/catalog.hpp"

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
 * @brief View model of the asset combo.
 *
 * The combo shows the name and writes that name. This class stores the id
 * through the DAL and emits `onAssetSelected`. `loadAssetsAsync` returns
 * immediately. The lambda it stores runs on the panel actor when a page arrives.
 */
class AssetList : public QObject {
    Q_OBJECT

  public:
    /**
     * @brief Publishes the empty asset combo.
     * @param panel Panel actor. Attributes are created on it, and loads are spawned through it.
     */
    explicit AssetList(plugin::StandardPlugin &panel);

    /**
     * @brief Stores a new selected id and emits `onAssetSelected`.
     *
     * Other attributes are ignored. Publishing the asset list does not emit
     * when the id is already the one stored.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value is a choice.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

    /**
     * @brief Loads asset pages when `assets_` is empty.
     *
     * Rows already on screen are kept. The catalog starts only when a project
     * is selected and the left tab is Asset. Returns before the pages arrive.
     */
    void loadAssetsAsync();

    /**
     * @brief Clears `assets_` and stops a load that is still running.
     *
     * The panel calls this when the selected project changes.
     */
    void clearAssets();

    /**
     * @brief Stores the asset id and emits `onAssetSelected`.
     * @param assetId Asset id, or `-1` when none is selected.
     */
    void setSelectedAsset(std::int64_t assetId);

    /**
     * @brief Handles an `asset` page and declines every other catalog.
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
     * @brief Publishes the asset combo.
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
     * @param payload `{id, name}` rows from the worker.
     */
    void updateAssetList(bool append, const utility::JsonStore &payload);

    /**
     * @brief Writes the asset total into the footer attribute.
     * @param payload JSON with an `assets` integer.
     */
    void updateAssetFooter(const utility::JsonStore &payload);

    /**
     * @brief Clears the footer attribute text.
     */
    void clearFooterValue();

    /**
     * @brief Writes the asset labels to the attribute the combo displays.
     */
    void sendAssetsListToQml();

    plugin::StandardPlugin &panel_; ///< Panel actor. Attributes and loads go through it.
    std::vector<poco::Asset> assets_; ///< Pages received for this combo.
    RequestGeneration asset_request_id_ =
        std::make_shared<std::atomic<std::uint64_t>>(0); ///< Drops stale asset fetches and pages.
    module::StringChoiceAttribute *asset_ = nullptr; ///< Labels the combo displays.
    module::StringAttribute *footer_ = nullptr; ///< Footer text for the QML list.
};

} // namespace xstudio::mikros_shotbrowser
