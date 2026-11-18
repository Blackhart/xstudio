// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/shot_browser.hpp
 * @brief Panel screens: tabs, and the view models those tabs are made of.
 */
#pragma once

#include "utility/catalog.hpp"

using namespace xstudio;

#include <QObject>

#include <caf/message_handler.hpp>

#include <memory>
#include <string>

namespace xstudio::mikros_shotbrowser {

class AssetList;
class PlaylistTree;
class ProjectSelector;
class ShotTree;
class VersionList;

/**
 * @brief View model that owns every screen of the panel.
 *
 * This is not the panel actor. It stores the two tabs, creates the other
 * screens, and forwards each attribute change and each loaded page to the
 * screen that owns it. The two tabs are stored by the DAL. The QML still writes the strings.
 */
class ShotBrowser : public QObject {
    Q_OBJECT

  public:
    /**
     * @brief Publishes the tabs, creates the screens, and connects their signals.
     *
     * Preferences are stored before the tab attributes exist, so startup does not emit.
     * The project list loads after the screen signals are connected.
     * @param panel Panel actor. Attributes are created on it, and each screen receives it.
     */
    explicit ShotBrowser(plugin::StandardPlugin &panel);

    /**
     * @brief Drops the screens. In-flight loads are ignored because the panel is gone.
     */
    ~ShotBrowser() override;

    /**
     * @brief Stores a tab, then forwards the change to each screen.
     *
     * The value already stored does not emit. A screen ignores an attribute it does not own.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value is a choice or a tab change.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

    /**
     * @brief Chains each screen's page handler, then the shared thumbnail page.
     * @return Handlers for the catalogs the screens own.
     */
    caf::message_handler message_handler_extensions();

  signals:
    /**
     * @brief Emitted after the left tab is stored from the QML.
     * @param mode `Shot` or `Asset`.
     */
    void onBrowserModeSelected(dal::BrowserMode mode);

    /**
     * @brief Emitted after the right tab is stored from the QML.
     * @param view `Versions` or `Playlist`.
     */
    void onResultViewSelected(ResultView view);

  private:
    /**
     * @brief Reads the two saved preferences and stores them without a signal.
     * @param panel Actor whose system holds the global store.
     */
    void restoreSavedTabs(plugin::StandardPlugin &panel);

    /**
     * @brief Publishes the left and right tab attributes from the values already stored.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

    /**
     * @brief Creates every screen and gives each the panel actor.
     */
    void initScreens();

    /**
     * @brief Connects each screen signal to the loads that follow it.
     */
    void initSignals();

    /**
     * @brief Loads the project list once the screens are connected.
     */
    void loadProjects();

    /**
     * @brief Stores the left tab written by the QML and emits when it changed.
     */
    void setBrowserMode();

    /**
     * @brief Stores the right tab written by the QML and emits when it changed.
     */
    void setResultView();

    plugin::StandardPlugin &panel_; ///< Panel actor passed to every screen.
    std::unique_ptr<ProjectSelector> project_selector_; ///< Project combo and `onProjectSelected`.
    std::unique_ptr<ShotTree> shot_tree_;               ///< Sequence tree and `onSequenceAndShotSelected`.
    std::unique_ptr<AssetList> asset_list_;             ///< Asset combo and `onAssetSelected`.
    std::unique_ptr<VersionList> version_list_;         ///< Versions, cards, and the open action.
    std::unique_ptr<PlaylistTree> playlist_tree_;       ///< Playlist tree.
    module::StringAttribute *browser_mode_ = nullptr; ///< `shot` or `asset` written by the QML.
    module::StringAttribute *result_view_attribute_ = nullptr; ///< `versions` or `playlist` written by the QML.
};

} // namespace xstudio::mikros_shotbrowser
