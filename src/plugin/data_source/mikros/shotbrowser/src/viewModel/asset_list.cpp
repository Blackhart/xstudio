// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/asset_list.cpp
 * @brief Asset combo: page load, selected id, and onAssetSelected.
 */

#include "viewModel/asset_list.hpp"

#include <caf/const_typed_message_view.hpp>
#include <caf/detail/behavior_impl.hpp>
#include <caf/make_counted.hpp>
#include <caf/unit.hpp>

#include <string>
#include <utility>

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

namespace {

using AssetPage = std::function<
    void(const std::string &, std::uint64_t, bool, const utility::JsonStore &)>;

/**
 * @brief Applies an asset page or count and declines every other catalog.
 *
 * `or_else` tries the next handler only when `invoke` returns false.
 */
class AssetPageHandler : public caf::detail::behavior_impl {
  public:
    /**
     * @brief Stores the lambda that applies one asset page or count.
     * @param apply Called on the panel actor for a current asset message.
     */
    explicit AssetPageHandler(AssetPage apply) : apply_(std::move(apply)) {}

    /**
     * @brief Runs the asset lambda, or declines the message.
     * @param visitor Receives an empty result when the page was ours.
     * @param message Worker page.
     * @return False when this handler does not take the message.
     */
    bool invoke(caf::detail::invoke_result_visitor &visitor, caf::message &message) override {
        using view_type = caf::const_typed_message_view<
            data_source::put_data_atom,
            std::uint64_t,
            std::string,
            std::int64_t,
            bool,
            std::int64_t,
            bool,
            std::int64_t,
            bool,
            std::int64_t,
            bool,
            std::int64_t,
            bool,
            utility::JsonStore>;
        view_type view{message};
        if (!view) {
            return false;
        }
        const std::string catalog = caf::get<2>(view);
        if (catalog != "asset" && catalog != "asset_count") {
            return false; ///< Another catalog: the next handler continues.
        }
        apply_(catalog, caf::get<1>(view), caf::get<12>(view), caf::get<13>(view));
        visitor(caf::unit);
        return true;
    }

  private:
    AssetPage apply_; ///< Lambda stored by `message_handler_extensions`.
};

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AssetList::AssetList(plugin::StandardPlugin &panel) : QObject(nullptr), panel_(panel) {
    initQmlAttributes(panel);
}

void AssetList::initQmlAttributes(plugin::StandardPlugin &panel) {
    asset_ = panel.add_string_choice_attribute("Asset", "Asset", "", {}, {});
    asset_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    footer_ = panel.add_string_attribute("AssetListFooter", "AssetListFooter", "");
    footer_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    assets_loaded_ = panel.add_boolean_attribute("AssetsLoaded", "AssetsLoaded", false);
    assets_loaded_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
}

// ---------------------------------------------------------------------------
// Selection. The combo writes a name. These functions store the id.
// ---------------------------------------------------------------------------

void AssetList::attribute_changed(const utility::Uuid &attr_uuid, int role) {
    if (role != module::Attribute::Value || !asset_ || attr_uuid != asset_->uuid()) {
        return; ///< Only the combo value is a choice.
    }
    applyAssetFromQml();
}

void AssetList::applyAssetFromQml() {
    const std::optional<poco::Asset> asset = findRowByLabel(assets_, asset_->value());
    setSelectedAsset(asset ? asset->id : -1);
}

void AssetList::setSelectedAsset(std::int64_t assetId) {
    if (assetId == dal::selectedAsset()) {
        return; ///< The id is already stored.
    }
    dal::setSelectedAsset(assetId); ///< Other screens read this id from the DAL.
    if (assetId == -1) {
        if (asset_ && !asset_->value().empty()) {
            asset_->set_value("", false); ///< Do not re-enter the asset handler.
        }
        trace::select("asset", false, 0, {});
    } else {
        std::optional<poco::Asset> asset;
        for (const poco::Asset &row : assets_) {
            if (row.id == assetId) {
                asset = row;
                break;
            }
        }
        logSelection("asset", asset);
    }
    emit onAssetSelected(assetId);
}

// ---------------------------------------------------------------------------
// Asset list. Non-empty assets_ is reused. A new project clears it first.
// ---------------------------------------------------------------------------

bool AssetList::checkProjectSelected() const {
    return dal::selectedProject() != -1;
}

bool AssetList::checkBrowserModeIsAsset() const {
    return dal::browserMode() == dal::BrowserMode::Asset;
}

void AssetList::loadAssetsAsync() {
    if (!assets_.empty() || !checkProjectSelected() || !checkBrowserModeIsAsset()) {
        return; ///< List already loaded, no project, or the shot tab is showing.
    }
    const std::uint64_t requestId = ++(*asset_request_id_); ///< Stop the asset fetch already running.
    const std::int64_t projectId = dal::selectedProject();
    CatalogLoadRequest pages =
        makeCatalogRequest(&panel_, CatalogKind::Asset, requestId, projectId);
    pages.generation = asset_request_id_; ///< The worker stops before the next find when this moves.
    launchCatalogLoad(panel_.system(), std::move(pages));
    CatalogLoadRequest counts =
        makeCatalogRequest(&panel_, CatalogKind::AssetCount, requestId, projectId);
    counts.generation = asset_request_id_; ///< Drop a stale footer with the pages.
    launchCatalogLoad(panel_.system(), std::move(counts));
}

void AssetList::clearAssetsTree() {
    ++(*asset_request_id_); ///< Drop pages that still belong to the previous project.
    assets_.clear();
    clearFooterValue(); ///< Clear until the next count arrives.
    sendAssetsListToQml();
    clearAssetsLoadedFlag();
}

void AssetList::clearSelectedAsset() {
    setSelectedAsset(-1);
}

void AssetList::updateAssetList(bool append, const utility::JsonStore &payload) {
    applyCatalogPage(assets_, asset_, rowsFromJson<poco::Asset>(payload), append);
    markAssetsLoaded();
}

namespace {

/**
 * @brief Formats the asset-list footer from a total.
 * @param total Match count.
 * @return `No assets`, `1 asset`, or `N assets`.
 */
std::string formatAssetFooter(int total) {
    if (total <= 0) {
        return "No assets";
    }
    if (total == 1) {
        return "1 asset";
    }
    return std::to_string(total) + " assets";
}

} // namespace

void AssetList::updateAssetFooter(const utility::JsonStore &payload) {
    if (!footer_) {
        return;
    }
    footer_->set_value(formatAssetFooter(payload.value("assets", 0)));
}

void AssetList::clearFooterValue() {
    if (footer_) {
        footer_->set_value("");
    }
}

void AssetList::clearAssetsLoadedFlag() {
    if (assets_loaded_) {
        assets_loaded_->set_value(false); ///< An empty list is a load in progress, not a result.
    }
}

void AssetList::markAssetsLoaded() {
    if (assets_loaded_) {
        assets_loaded_->set_value(true); ///< Zero rows is still a finished load.
    }
}

void AssetList::sendAssetsListToQml() {
    if (!asset_) {
        return;
    }
    asset_->set_role_data(module::Attribute::StringChoices, buildUniqueLabels(assets_));
}

// ---------------------------------------------------------------------------
// Pages. The worker mail is accepted here, then handed to the list.
// ---------------------------------------------------------------------------

caf::message_handler AssetList::message_handler_extensions() {
    auto apply = [this](
                     const std::string &catalog,
                     std::uint64_t requestId,
                     bool append,
                     const utility::JsonStore &payload) {
        if (requestId != asset_request_id_->load()) {
            trace::drop("asset", requestId);
            return;
        }
        if (catalog == "asset_count") {
            updateAssetFooter(payload);
            return;
        }
        updateAssetList(append, payload);
    };
    caf::detail::behavior_impl::pointer handler = caf::make_counted<AssetPageHandler>(std::move(apply));
    return caf::message_handler{std::move(handler)};
}

} // namespace xstudio::mikros_shotbrowser
