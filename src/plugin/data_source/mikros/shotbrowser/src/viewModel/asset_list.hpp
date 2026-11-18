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
#include <functional>
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
     * @brief Stops the previous asset fetch, clears the combo, then loads on the Asset tab.
     *
     * The request id always moves. The catalog starts only when a project is selected
     * and the left tab is Asset. Returns before the pages arrive.
     */
    void loadAssetsAsync();

    /**
     * @brief Selects no asset and clears the combo without a signal.
     */
    void clearAssetSelected();

    /**
     * @brief Clears the asset when a sequence or shot was chosen.
     * @param parentId Sequence or shot id. `-1` leaves the asset alone.
     */
    void clearAssetSelected(std::int64_t parentId);

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
     * @brief Empties the asset list and selects no asset.
     */
    void clearAssetList();

    /**
     * @brief Replaces or extends `assets_` with one page, then publishes it.
     * @param append False for the first page of a request.
     * @param payload `{id, name}` rows from the worker.
     */
    void updateAssetList(bool append, const utility::JsonStore &payload);

    plugin::StandardPlugin &panel_; ///< Panel actor. Attributes and loads go through it.
    std::vector<poco::Asset> assets_; ///< Pages received for this combo.
    RequestGeneration asset_request_id_ =
        std::make_shared<std::atomic<std::uint64_t>>(0); ///< Drops stale asset fetches and pages.
    std::function<void(std::uint64_t, bool, const utility::JsonStore &)> page_; ///< Applies one page on the panel actor.
    module::StringChoiceAttribute *asset_ = nullptr; ///< Labels the combo displays.
};

} // namespace xstudio::mikros_shotbrowser
