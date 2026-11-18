// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/project_selector.hpp
 * @brief Project combo: publishes id and name, and signals the chosen id.
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
 * @brief View model of the project combo.
 *
 * The combo shows the name and writes the id. `None` writes `-1`.
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
     * @brief Stores a new selected id and emits `onProjectSelected`.
     *
     * Other attributes are ignored. Publishing the project list does not emit.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value is a choice.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

    /**
     * @brief Clears the combo, then loads project pages on a detached actor.
     *
     * Returns before the pages arrive. Each page is applied by the lambda stored here.
     */
    void loadProjectsAsync();

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
     * @brief Publishes the project catalog and the selected id.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

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
     * @brief Empties the project list and selects None.
     *
     * The empty list is sent to the combo. None is sent on the selected-id attribute.
     */
    void clearProjectsList();

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
     * @brief Writes `{id, name}` rows to the attribute the combo displays.
     */
    void sendProjectsListToQml();

    plugin::StandardPlugin &panel_; ///< Panel actor. Attributes and loads go through it.
    std::vector<poco::Project> projects_; ///< Pages received for this combo.
    RequestGeneration project_request_id_ =
        std::make_shared<std::atomic<std::uint64_t>>(0); ///< Drops stale project fetches and pages.
    std::function<void(std::uint64_t, bool, const utility::JsonStore &)> page_; ///< Applies one page on the panel actor.
    module::JsonAttribute *catalog_ = nullptr; ///< `{id, name}` rows the combo displays.
    module::StringAttribute *selected_ = nullptr; ///< Id the combo wrote, as text. `-1` is None.
};

} // namespace xstudio::mikros_shotbrowser
