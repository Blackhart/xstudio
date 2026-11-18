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

#include <utility>

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

namespace {

using AssetPage = std::function<void(std::uint64_t, bool, const utility::JsonStore &)>;

/**
 * @brief Applies an asset page and declines every other catalog.
 *
 * `or_else` tries the next handler only when `invoke` returns false.
 */
class AssetPageHandler : public caf::detail::behavior_impl {
  public:
    /**
     * @brief Stores the lambda that applies one asset page.
     * @param apply Called on the panel actor for a current asset page.
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
        if (caf::get<2>(view) != "asset") {
            return false; ///< Another catalog: the next handler continues.
        }
        apply_(caf::get<1>(view), caf::get<12>(view), caf::get<13>(view));
        visitor(caf::unit);
        return true;
    }

  private:
    AssetPage apply_; ///< Lambda stored by `loadAssetsAsync`.
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
    const std::int64_t assetId = asset ? asset->id : -1;
    if (assetId == dal::selectedAsset()) {
        return; ///< The QML write repeats an id already stored.
    }
    dal::setSelectedAsset(assetId); ///< Other screens read this id from the DAL.
    logSelection("asset", asset);
    emit onAssetSelected(assetId);
}

void AssetList::clearAssetSelected() {
    const bool hadSelection = dal::selectedAsset() != -1 || (asset_ && !asset_->value().empty());
    dal::setSelectedAsset(-1);
    if (asset_ && !asset_->value().empty()) {
        asset_->set_value("", false); ///< Do not re-enter the asset handler.
    }
    if (hadSelection) {
        trace::select("asset", false, 0, {});
    }
}

void AssetList::clearAssetSelected(std::int64_t parentId) {
    if (parentId == -1) {
        return; ///< The sequence or shot was cleared. Keep the asset.
    }
    clearAssetSelected(); ///< Sequence and asset are mutually exclusive.
}

// ---------------------------------------------------------------------------
// Asset list. A load clears the combo, then each page extends it.
// ---------------------------------------------------------------------------

void AssetList::loadAssetsAsync() {
    const std::uint64_t requestId = ++(*asset_request_id_); ///< Stop the asset fetch already running.
    page_ = [this](std::uint64_t id, bool append, const utility::JsonStore &payload) {
        if (id != asset_request_id_->load()) {
            trace::drop("asset", id);
            return;
        }
        updateAssetList(append, payload);
    };
    clearAssetList();
    if (dal::selectedProject() == -1 || dal::browserMode() != dal::BrowserMode::Asset) {
        return; ///< No project, or the shot tab is showing.
    }
    CatalogLoadRequest request = makeCatalogRequest(
        &panel_, CatalogKind::Asset, requestId, dal::selectedProject());
    request.generation = asset_request_id_; ///< The worker stops before the next find when this moves.
    launchCatalogLoad(panel_.system(), std::move(request));
}

void AssetList::clearAssetList() {
    assets_.clear();
    if (asset_) {
        asset_->set_role_data(module::Attribute::StringChoices, std::vector<std::string>{});
    }
    clearAssetSelected();
}

void AssetList::updateAssetList(bool append, const utility::JsonStore &payload) {
    applyCatalogPage(assets_, asset_, rowsFromJson<poco::Asset>(payload), append);
}

// ---------------------------------------------------------------------------
// Pages. The worker mail is accepted here, then handed to the list.
// ---------------------------------------------------------------------------

caf::message_handler AssetList::message_handler_extensions() {
    auto apply = [this](std::uint64_t requestId, bool append, const utility::JsonStore &payload) {
        if (page_) {
            page_(requestId, append, payload); ///< The lambda from the latest loadAssetsAsync.
        }
    };
    caf::detail::behavior_impl::pointer handler = caf::make_counted<AssetPageHandler>(std::move(apply));
    return caf::message_handler{std::move(handler)};
}

} // namespace xstudio::mikros_shotbrowser
