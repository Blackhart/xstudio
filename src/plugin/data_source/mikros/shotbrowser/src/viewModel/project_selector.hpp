// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/project_selector.hpp
 * @brief Project combo: publishes id and name, and filters by status and type.
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
#include <set>
#include <string>
#include <vector>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief View model of the project combo.
 *
 * The combo shows the name and writes the id. `None` writes `-1`.
 * Checked statuses and types are the projects the combo shows. An empty
 * check set leaves that field unfiltered.
 * `loadProjectsAsync` returns immediately. The lambda it stores runs on the
 * panel actor when a project page arrives.
 */
class ProjectSelector : public QObject {
    Q_OBJECT

  public:
    /**
     * @brief Publishes the empty project list and the selected id.
     * @param panel Panel actor. Attributes are created on it, and loads are spawned through it.
     */
    explicit ProjectSelector(plugin::StandardPlugin &panel);

    /**
     * @brief Stores a new selected id, or applies the status and type checks.
     *
     * Publishing the project list does not emit. A filter write republishes
     * the combo and clears the selection when that project is hidden.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value is a choice.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

    /**
     * @brief Loads project pages when `projects_` is empty.
     *
     * Rows already on screen are kept. Returns before the pages arrive. Each
     * page is applied by the lambda stored here.
     */
    void loadProjectsAsync();

    /**
     * @brief Empties the project list and selects None.
     *
     * The empty list is sent to the combo. None is sent on the selected-id attribute.
     */
    void clearProjects();

    /**
     * @brief Handles a `project` page and declines every other catalog.
     *
     * The shell chains this with `or_else`. A declined page is left for the next handler.
     * @return The project-page handler.
     */
    caf::message_handler message_handler_extensions();

  signals:
    /**
     * @brief Emitted after the selected id is stored.
     * @param projectId Project id, or `-1` when the combo is on None.
     */
    void onProjectSelected(std::int64_t projectId);

  private:
    /**
     * @brief Creates the project catalog, selection, and filter attributes.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

    /**
     * @brief Applies the schema `show` lists to the filter attributes and sets.
     *
     * Call after `initQmlAttributes`. Empty schema lists leave the combo unfiltered.
     */
    void setDefaultValues();

    /**
     * @brief Checks the statuses declared in schema `project.status.show`.
     */
    void setDefaultProjectShown();

    /**
     * @brief Checks the types declared in schema `project.type.show`.
     */
    void setDefaultProjectType();

    /**
     * @brief Reads the id written by the combo.
     * @return The id, or `-1` when the text is empty or not an integer.
     */
    std::int64_t readSelectedProjectFromQml() const;

    /**
     * @brief Stores the project id and emits `onProjectSelected`.
     * @param projectId Project id, or `-1` when the combo is on None.
     */
    void setSelectedProject(std::int64_t projectId);

    /**
     * @brief Selects None and sends that id to the combo.
     */
    void clearProjectSelected();

    /**
     * @brief Writes the selected project id to the attribute the combo reads.
     */
    void sendProjectSelectedToQml();

    /**
     * @brief Name of one loaded project.
     * @param projectId Id to look up.
     * @return The name, or empty when the id is not in `projects_`.
     */
    std::string nameOf(std::int64_t projectId) const;

    /**
     * @brief Replaces or extends `projects_` with one page, then publishes it.
     *
     * The catalog is sorted by name before it is sent to the combo.
     * @param append False for the first page of a request.
     * @param payload JSON rows from the worker.
     */
    void updateProjectList(bool append, const utility::JsonStore &payload);

    /**
     * @brief Sorts `projects_` by name.
     *
     * Names follow the system locale and ignore case. Equal names keep the smaller id first.
     */
    void sortProjectsByName();

    /**
     * @brief Writes the projects that pass the checks to the combo.
     *
     * Each row is `{id, name}`. Status and type stay on `projects_`.
     */
    void sendProjectsListToQml();

    /**
     * @brief True when the attribute is `ShownStatuses` or `ShownTypes`.
     * @param attr_uuid Attribute the panel changed.
     * @return True when that write is a filter check.
     */
    bool isShownFilter(const utility::Uuid &attr_uuid) const;

    /**
     * @brief Reads the checked values and republishes the combo.
     *
     * A repeated write of the same checks does nothing. A hidden selection
     * becomes None.
     */
    void applyShownFilters();

    /**
     * @brief True when this project is inside every non-empty check set.
     * @param project Loaded project.
     * @return False when a checked status or type excludes it.
     */
    bool projectPassesFilters(const poco::Project &project) const;

    /**
     * @brief Selects None when the current project is filtered out.
     */
    void clearSelectionWhenHidden();

    /**
     * @brief Publishes the distinct status and type values the menu lists.
     */
    void publishFilterOptions();

    plugin::StandardPlugin &panel_; ///< Panel actor. Attributes and loads go through it.
    std::vector<poco::Project> projects_; ///< Pages received for this combo.
    std::set<std::string> shown_statuses_; ///< Checked statuses. Empty shows every status.
    std::set<std::string> shown_types_;    ///< Checked types. Empty shows every type.
    RequestGeneration project_request_id_ =
        std::make_shared<std::atomic<std::uint64_t>>(0); ///< Drops stale project fetches and pages.
    std::function<void(std::uint64_t, bool, const utility::JsonStore &)> page_; ///< Applies one page on the panel actor.
    module::JsonAttribute *catalog_ = nullptr; ///< `{id, name}` rows the combo displays.
    module::StringAttribute *selected_ = nullptr; ///< Id the combo wrote, as text. `-1` is None.
    module::JsonAttribute *status_options_ = nullptr; ///< Distinct statuses the menu lists.
    module::JsonAttribute *type_options_ = nullptr;   ///< Distinct types the menu lists.
    module::JsonAttribute *shown_statuses_attribute_ = nullptr; ///< Statuses the menu checked.
    module::JsonAttribute *shown_types_attribute_ = nullptr;    ///< Types the menu checked.
};

} // namespace xstudio::mikros_shotbrowser
