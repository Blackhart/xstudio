// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/asset_list.cpp
 * @brief Asset list: first page, load-more, name/id search, and selected id.
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

/**
 * @brief Formats the asset-list footer as loaded rows over the server total.
 * @param loaded Rows currently held by the list.
 * @param total Match count from the server, or -1 when the count has not arrived.
 * @return Footer text (`N / T assets`), or `No assets` when total is zero.
 */
std::string formatAssetFooter(std::size_t loaded, int total) {
    if (total == 0) {
        return "No assets";
    }
    if (total < 0) {
        return std::to_string(loaded) + " loaded";
    }
    return std::to_string(loaded) + " / " + std::to_string(total) + " assets";
}

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
    load_more_assets_ = panel.add_string_attribute("LoadMoreAssets", "LoadMoreAssets", "");
    load_more_assets_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    asset_search_ = panel.add_string_attribute("AssetSearch", "AssetSearch", "");
    asset_search_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
}

// ---------------------------------------------------------------------------
// Selection. The combo writes a name. These functions store the id.
// ---------------------------------------------------------------------------

void AssetList::attribute_changed(const utility::Uuid &attr_uuid, int role) {
    if (role != module::Attribute::Value) {
        return;
    }
    if (load_more_assets_ && attr_uuid == load_more_assets_->uuid()) {
        loadMoreAssetsAsync(); ///< QML bumped the attribute near the bottom of the list.
        return;
    }
    if (asset_search_ && attr_uuid == asset_search_->uuid()) {
        clearAssetsTree(); ///< Drop the previous filter result before the new first page.
        loadAssetsFromNameOrIdAsync(asset_search_->value());
        return;
    }
    if (!asset_ || attr_uuid != asset_->uuid()) {
        return; ///< Only the combo value is a choice.
    }
    applyAssetFromQml();
}

std::optional<std::string> AssetList::activeSearchQuery() const {
    if (search_query_.empty()) {
        return std::nullopt;
    }
    return search_query_;
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
    next_cursor_.reset(); ///< A new first page replaces any pending continuation.
    more_incoming_ = false;
    const std::uint64_t requestId = ++(*asset_request_id_); ///< Stop the asset fetch already running.
    const std::int64_t projectId = dal::selectedProject();
    const std::optional<std::string> nameOrId = activeSearchQuery();
    CatalogLoadRequest pages =
        makeCatalogRequest(&panel_, CatalogKind::Asset, requestId, projectId);
    pages.name_or_id = nameOrId;
    pages.generation = asset_request_id_; ///< The worker stops before the next find when this moves.
    launchCatalogLoad(panel_.system(), std::move(pages));
    CatalogLoadRequest counts =
        makeCatalogRequest(&panel_, CatalogKind::AssetCount, requestId, projectId);
    counts.name_or_id = nameOrId;
    counts.generation = asset_request_id_; ///< Drop a stale footer with the pages.
    launchCatalogLoad(panel_.system(), std::move(counts));
}

void AssetList::loadAssetsFromNameOrIdAsync(const std::string &text) {
    search_query_ = text; ///< Empty clears the filter; the DAL trims before Where.
    loadAssetsAsync();
}

void AssetList::loadMoreAssetsAsync() {
    if (dal::selectedProject() == -1 || !next_cursor_ || more_incoming_) {
        return; ///< Nothing left to fetch, or a more-page is already on the way.
    }
    more_incoming_ = true;
    const std::uint64_t requestId = asset_request_id_->load(); ///< Same generation as the first page.
    CatalogLoadRequest request =
        makeCatalogRequest(&panel_, CatalogKind::Asset, requestId, dal::selectedProject());
    request.cursor = next_cursor_;
    request.name_or_id = activeSearchQuery();
    request.generation = asset_request_id_; ///< Drop the page when clearAssetsTree moves the id.
    launchCatalogLoad(panel_.system(), std::move(request));
}

void AssetList::clearAssetsTree() {
    ++(*asset_request_id_); ///< Drop pages that still belong to the previous project.
    next_cursor_.reset();
    assets_.clear();
    clearFooterValue(); ///< Clear until the next count arrives.
    sendAssetsListToQml();
    clearAssetsLoadedFlag();
    more_incoming_ = false;
}

void AssetList::clearSelectedAsset() {
    setSelectedAsset(-1);
}

void AssetList::updateAssetList(bool append, const utility::JsonStore &payload) {
    const utility::JsonStore rowsJson =
        payload.contains("rows") ? utility::JsonStore(payload.at("rows")) : payload;
    if (payload.contains("next_cursor") && payload.at("next_cursor").is_string()) {
        next_cursor_ = payload.at("next_cursor").get<std::string>();
    } else {
        next_cursor_.reset(); ///< Last page, or a payload without a continuation.
    }
    applyCatalogPage(assets_, asset_, rowsFromJson<poco::Asset>(rowsJson), append);
    refreshFooterValue(); ///< Show how many rows are loaded versus the server total.
    markAssetsLoaded();
    more_incoming_ = false; ///< This page arrived; another more-request may start.
}

void AssetList::updateAssetFooter(const utility::JsonStore &payload) {
    asset_total_ = payload.value("assets", 0);
    refreshFooterValue();
}

void AssetList::refreshFooterValue() {
    if (!footer_) {
        return;
    }
    footer_->set_value(formatAssetFooter(assets_.size(), asset_total_));
}

void AssetList::clearFooterValue() {
    asset_total_ = -1;
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
