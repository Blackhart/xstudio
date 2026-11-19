// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/shot_browser.cpp
 * @brief Panel tabs restored from preferences, then signaled when the user changes them.
 */

#include "viewModel/shot_browser.hpp"
#include "viewModel/asset_list.hpp"
#include "viewModel/playlist_tree.hpp"
#include "viewModel/project_selector.hpp"
#include "viewModel/shot_tree.hpp"
#include "utility/thumbnails.hpp"
#include "viewModel/version_list.hpp"

#include "xstudio/global_store/global_store.hpp"

#include <QObject>

#include <exception>
#include <optional>

namespace xstudio::mikros_shotbrowser {

namespace {

const char *kBrowserModePreference = "/plugin/data_source/mikros_shotbrowser/browser_mode";
const char *kResultViewPreference = "/plugin/data_source/mikros_shotbrowser/result_view";
const char *kAttrsGroup = "mikros_shotbrowser_attributes";
const char *kBrowserModeAttr = "BrowserMode";
const char *kResultViewAttr = "ResultView";
const char *kBrowserModeShot = "shot";
const char *kBrowserModeAsset = "asset";
const char *kResultViewVersions = "versions";
const char *kResultViewPlaylist = "playlist";
const char *kVersionThumbsCatalog = "version_thumbs";

/**
 * @brief Left tab named by a preference or by the QML.
 * @param text `shot`, `asset`, or anything else.
 * @return The tab, or empty when the text is not one of the two.
 */
std::optional<dal::BrowserMode> browserModeFromText(const std::string &text) {
    if (text == kBrowserModeShot) {
        return dal::BrowserMode::Shot;
    }
    if (text == kBrowserModeAsset) {
        return dal::BrowserMode::Asset;
    }
    return std::nullopt;
}

/**
 * @brief Right tab named by a preference or by the QML.
 * @param text `versions`, `playlist`, or anything else.
 * @return The tab, or empty when the text is not one of the two.
 */
std::optional<ResultView> resultViewFromText(const std::string &text) {
    if (text == kResultViewVersions) {
        return ResultView::Versions;
    }
    if (text == kResultViewPlaylist) {
        return ResultView::Playlist;
    }
    return std::nullopt;
}

/**
 * @brief Text the QML attribute stores for one left tab.
 * @param mode Tab to show.
 * @return `shot` or `asset`.
 */
const char *textOfBrowserMode(dal::BrowserMode mode) {
    return mode == dal::BrowserMode::Asset ? kBrowserModeAsset : kBrowserModeShot;
}

/**
 * @brief Text the QML attribute stores for one right tab.
 * @param view Tab to show.
 * @return `versions` or `playlist`.
 */
const char *textOfResultView(ResultView view) {
    return view == ResultView::Playlist ? kResultViewPlaylist : kResultViewVersions;
}

/**
 * @brief One saved preference string.
 * @param panel Actor whose system holds the global store.
 * @param path Preference path.
 * @return The saved text, or empty when that path cannot be read.
 */
std::string preferenceText(plugin::StandardPlugin &panel, const char *path) {
    try {
        global_store::GlobalStoreHelper preferences(panel.system());
        return preferences.value<std::string>(path);
    } catch (const std::exception &) {
        return {}; ///< An unknown path keeps the default tab.
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ShotBrowser::ShotBrowser(plugin::StandardPlugin &panel) : QObject(nullptr), panel_(panel) {
    restoreSavedTabs(panel); ///< Before the attributes exist, so startup does not emit.
    initQmlAttributes(panel);
    initScreens();
    initSignals(); ///< Before the first clear, so an empty project still reloads the screens.
    loadProjects();
}

ShotBrowser::~ShotBrowser() = default;

void ShotBrowser::restoreSavedTabs(plugin::StandardPlugin &panel) {
    if (const std::optional<dal::BrowserMode> mode =
            browserModeFromText(preferenceText(panel, kBrowserModePreference))) {
        dal::setBrowserMode(*mode); ///< A string other than shot or asset leaves Shot.
    }
    if (const std::optional<ResultView> view =
            resultViewFromText(preferenceText(panel, kResultViewPreference))) {
        dal::setResultView(*view); ///< A string other than versions or playlist leaves Versions.
    }
}

void ShotBrowser::initQmlAttributes(plugin::StandardPlugin &panel) {
    browser_mode_ = panel.add_string_attribute(
        kBrowserModeAttr, kBrowserModeAttr, textOfBrowserMode(dal::browserMode()));
    browser_mode_->expose_in_ui_attrs_group(kAttrsGroup);
    result_view_attribute_ = panel.add_string_attribute(
        kResultViewAttr, kResultViewAttr, textOfResultView(dal::resultView()));
    result_view_attribute_->expose_in_ui_attrs_group(kAttrsGroup);
}

void ShotBrowser::initScreens() {
    project_selector_ = std::make_unique<ProjectSelector>(panel_);
    shot_tree_ = std::make_unique<ShotTree>(panel_);
    asset_list_ = std::make_unique<AssetList>(panel_);
    version_list_ = std::make_unique<VersionList>(panel_);
    playlist_tree_ = std::make_unique<PlaylistTree>(panel_);
}

void ShotBrowser::loadProjects() {
    if (project_selector_) {
        project_selector_->loadProjectsAsync(); ///< First catalog, before the panel asks.
    }
}

// ---------------------------------------------------------------------------
// Signals. Each screen reload follows the choice that triggered it.
// ---------------------------------------------------------------------------

void ShotBrowser::initSignals() {
    // Project chosen: clear the open sequence, then reload sequences, assets, versions, and playlists.
    QObject::connect(
        project_selector_.get(),
        &ProjectSelector::onProjectSelected,
        this,
        [this](std::int64_t) {
            shot_tree_->clearSequenceShots(); ///< The previous project's tree must not be reused.
            shot_tree_->loadSequenceShotsAsync();
            asset_list_->loadAssetsAsync();
            version_list_->loadVersionsAsync();
            playlist_tree_->loadPlaylistListAsync(dal::resultView());
        },
        Qt::DirectConnection);
    // Left tab: reload the sequence tree on Shot, or the asset list on Asset.
    QObject::connect(
        this,
        &ShotBrowser::onBrowserModeSelected,
        this,
        [this](dal::BrowserMode) {
            shot_tree_->loadSequenceShotsAsync(); ///< Loads only on the Shot tab, and keeps a tree already stored.
            asset_list_->loadAssetsAsync();       ///< Loads only on the Asset tab.
        },
        Qt::DirectConnection);
    // Right tab: reload the playlist names when the view is Playlist.
    QObject::connect(
        this,
        &ShotBrowser::onResultViewSelected,
        this,
        [this](ResultView view) { playlist_tree_->loadPlaylistListAsync(view); },
        Qt::DirectConnection);
    // Sequence/shot pair chosen: drop the asset when needed, then reload versions and the open playlist.
    QObject::connect(
        shot_tree_.get(),
        &ShotTree::onSequenceAndShotSelected,
        this,
        [this](std::int64_t sequenceId, std::int64_t shotId) {
            if (sequenceId != -1 || shotId != -1) {
                asset_list_->clearAssetSelected(sequenceId != -1 ? sequenceId : shotId);
            }
            version_list_->loadVersionsAsync();
            playlist_tree_->reloadExpanded(dal::resultView());
        },
        Qt::DirectConnection);
    // Asset chosen: reload versions and the open playlist (the pair was cleared at the tab switch).
    QObject::connect(
        asset_list_.get(),
        &AssetList::onAssetSelected,
        this,
        [this](std::int64_t) {
            version_list_->loadVersionsAsync();
            playlist_tree_->reloadExpanded(dal::resultView());
        },
        Qt::DirectConnection);
}

// ---------------------------------------------------------------------------
// Tabs. The QML writes a string. These functions store the enum and signal it.
// ---------------------------------------------------------------------------

void ShotBrowser::attribute_changed(const utility::Uuid &attr_uuid, int role) {
    if (role != module::Attribute::Value) {
        return;
    }
    if (browser_mode_ && attr_uuid == browser_mode_->uuid()) {
        setBrowserMode(); ///< Left tab, then onBrowserModeSelected.
    } else if (result_view_attribute_ && attr_uuid == result_view_attribute_->uuid()) {
        setResultView(); ///< Right tab, then onResultViewSelected.
    }
    if (project_selector_) {
        project_selector_->attribute_changed(attr_uuid, role); ///< Selected id, then onProjectSelected.
    }
    if (shot_tree_) {
        shot_tree_->attribute_changed(attr_uuid, role); ///< Sequence, shot, or an unfolded sequence.
    }
    if (asset_list_) {
        asset_list_->attribute_changed(attr_uuid, role); ///< Selected asset, then onAssetSelected.
    }
    if (playlist_tree_) {
        playlist_tree_->attribute_changed(attr_uuid, role); ///< An unfolded playlist.
    }
    if (version_list_) {
        version_list_->attribute_changed(attr_uuid, role); ///< A double-click or a menu open.
    }
}

void ShotBrowser::setBrowserMode() {
    if (!browser_mode_) {
        return;
    }
    const std::optional<dal::BrowserMode> mode = browserModeFromText(browser_mode_->value());
    if (!mode || *mode == dal::browserMode()) {
        return; ///< The QML write repeats the tab already stored, or the text is unknown.
    }
    dal::setBrowserMode(*mode);
    emit onBrowserModeSelected(*mode);
}

void ShotBrowser::setResultView() {
    if (!result_view_attribute_) {
        return;
    }
    const std::optional<ResultView> view = resultViewFromText(result_view_attribute_->value());
    if (!view || *view == dal::resultView()) {
        return; ///< The QML write repeats the tab already stored, or the text is unknown.
    }
    dal::setResultView(*view);
    emit onResultViewSelected(*view);
}

// ---------------------------------------------------------------------------
// Pages. Each screen takes its catalog. Thumbnails refresh both lists.
// ---------------------------------------------------------------------------

caf::message_handler ShotBrowser::message_handler_extensions() {
    caf::message_handler projects;
    if (project_selector_) {
        projects = project_selector_->message_handler_extensions();
    }
    caf::message_handler shotTree;
    if (shot_tree_) {
        shotTree = shot_tree_->message_handler_extensions();
    }
    caf::message_handler assets;
    if (asset_list_) {
        assets = asset_list_->message_handler_extensions();
    }
    caf::message_handler versions;
    if (version_list_) {
        versions = version_list_->message_handler_extensions();
    }
    caf::message_handler playlists;
    if (playlist_tree_) {
        playlists = playlist_tree_->message_handler_extensions();
    }
    return projects.or_else(shotTree)
        .or_else(assets)
        .or_else(versions)
        .or_else(playlists)
        .or_else([this](data_source::put_data_atom,
                     const std::uint64_t,
                     const std::string &catalog,
                     const std::int64_t projectId,
                     const bool,
                     const std::int64_t,
                     const bool,
                     const std::int64_t,
                     const bool,
                     const std::int64_t,
                     const bool,
                     const std::int64_t,
                     const bool,
                     const utility::JsonStore &payload) {
            if (catalog != kVersionThumbsCatalog) {
                return; ///< Another catalog was already declined by its screen.
            }
            rememberThumbs(projectId, payload); ///< Keep the file even if the filter moved on.
            if (dal::selectedProject() != projectId) {
                return;
            }
            if (version_list_) {
                version_list_->sendVersionCardsToQml();
            }
            if (playlist_tree_) {
                playlist_tree_->stampPlaylistThumbs();
            }
        });
}

} // namespace xstudio::mikros_shotbrowser
