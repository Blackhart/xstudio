// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/project_selector.cpp
 * @brief Project combo: page load, selected id, and onProjectSelected.
 */

#include "viewModel/project_selector.hpp"

#include <caf/const_typed_message_view.hpp>
#include <caf/detail/behavior_impl.hpp>
#include <caf/make_counted.hpp>
#include <caf/unit.hpp>

#include <QCollator>
#include <QString>

#include <algorithm>
#include <exception>
#include <utility>

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

namespace {

using ProjectPage = std::function<void(std::uint64_t, bool, const utility::JsonStore &)>;

/**
 * @brief Applies a project page and declines every other catalog.
 *
 * `or_else` tries the next handler only when `invoke` returns false. Returning
 * `caf::skip` from a matched functor still consumes the message, so a page for
 * another catalog is declined by returning false.
 */
class ProjectPageHandler : public caf::detail::behavior_impl {
  public:
    /**
     * @brief Stores the lambda that applies one project page.
     * @param apply Called on the panel actor for a current project page.
     */
    explicit ProjectPageHandler(ProjectPage apply) : apply_(std::move(apply)) {}

    /**
     * @brief Runs the project lambda, or declines the message.
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
        if (caf::get<2>(view) != "project") {
            return false; ///< Another catalog: the shell handler continues.
        }
        apply_(caf::get<1>(view), caf::get<12>(view), caf::get<13>(view));
        visitor(caf::unit);
        return true;
    }

  private:
    ProjectPage apply_; ///< Lambda stored by `loadProjectsAsync`.
};

/**
 * @brief Orders one project before another by name.
 * @param left First project.
 * @param right Second project.
 * @return True when `left` belongs before `right`.
 */
bool projectNameAlphabeticOrderAlgorithm(const poco::Project &left, const poco::Project &right) {
    static const QCollator order = [] {
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive); ///< `a` comes before `B`.
        return collator;
    }();
    const int nameOrder = order.compare(
        QString::fromStdString(left.name), QString::fromStdString(right.name));
    if (nameOrder != 0) {
        return nameOrder < 0; ///< Different names: the earlier name comes first.
    }
    return left.id < right.id; ///< Same name: the smaller id stays put.
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ProjectSelector::ProjectSelector(plugin::StandardPlugin &panel)
    : QObject(nullptr), panel_(panel) {
    initQmlAttributes(panel);
}

void ProjectSelector::initQmlAttributes(plugin::StandardPlugin &panel) {
    catalog_ = panel.add_json_attribute("Projects", "Projects", nlohmann::json::array());
    catalog_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    selected_ = panel.add_string_attribute("SelectedProject", "SelectedProject", "-1");
    selected_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
}

// ---------------------------------------------------------------------------
// Selection. The combo writes an id. These functions store it and send it back.
// ---------------------------------------------------------------------------

void ProjectSelector::attribute_changed(const utility::Uuid &attr_uuid, int role) {
    if (role != module::Attribute::Value || !selected_ || attr_uuid != selected_->uuid()) {
        return; ///< The project list is published on another attribute.
    }
    const std::int64_t projectId = readSelectedProjectFromQml();
    if (projectId == dal::selectedProject()) {
        return; ///< The QML write repeats an id already stored.
    }
    setSelectedProject(projectId);
}

std::int64_t ProjectSelector::readSelectedProjectFromQml() const {
    if (!selected_) {
        return -1;
    }
    const std::string text = selected_->value();
    if (text.empty()) {
        return -1;
    }
    try {
        return std::stoll(text);
    } catch (const std::exception &) {
        return -1; ///< A value the combo did not write is None.
    }
}

void ProjectSelector::setSelectedProject(std::int64_t projectId) {
    dal::setSelectedProject(projectId); ///< Slots read the id that was just chosen.
    trace::select(
        "project", projectId != -1, projectId == -1 ? 0 : projectId, nameOf(projectId));
    emit onProjectSelected(projectId);
}

void ProjectSelector::clearProjectSelected() {
    setSelectedProject(-1);
    sendProjectSelectedToQml();
}

void ProjectSelector::sendProjectSelectedToQml() {
    if (!selected_) {
        return;
    }
    selected_->set_value(std::to_string(dal::selectedProject())); ///< The combo reads this attribute.
}

std::string ProjectSelector::nameOf(std::int64_t projectId) const {
    for (const poco::Project &project : projects_) {
        if (project.id == projectId) {
            return project.name;
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Project list. Non-empty projects_ is reused. clearProjects empties it first.
// ---------------------------------------------------------------------------

void ProjectSelector::loadProjectsAsync() {
    if (!projects_.empty()) {
        return; ///< The combo already has the project list.
    }
    const std::uint64_t requestId = ++(*project_request_id_); ///< Drop pages from the previous project load.
    page_ = [this](std::uint64_t id, bool append, const utility::JsonStore &payload) {
        if (id != project_request_id_->load()) {
            trace::drop("project", id);
            return;
        }
        updateProjectList(append, payload);
    };
    CatalogLoadRequest request =
        makeCatalogRequest(&panel_, CatalogKind::Project, requestId, 0);
    request.generation = project_request_id_; ///< The worker stops before the next find when this moves.
    launchCatalogLoad(panel_.system(), std::move(request));
}

void ProjectSelector::clearProjects() {
    ++(*project_request_id_); ///< Drop pages from the previous project load.
    projects_.clear();
    sendProjectsListToQml(); ///< The combo shows Loading until the next page.
    clearProjectSelected();
}

void ProjectSelector::updateProjectList(bool append, const utility::JsonStore &payload) {
    std::vector<poco::Project> rows = rowsFromJson<poco::Project>(payload);
    if (!append) {
        projects_ = std::move(rows); ///< The first page replaces the catalog.
    } else if (!rows.empty()) {
        projects_.insert(projects_.end(), rows.begin(), rows.end());
    }
    sortProjectsByName(); ///< The combo shows this page in alphabetical order.
    sendProjectsListToQml();
}

void ProjectSelector::sortProjectsByName() {
    std::sort(projects_.begin(), projects_.end(), projectNameAlphabeticOrderAlgorithm);
}

void ProjectSelector::sendProjectsListToQml() {
    nlohmann::json rows = nlohmann::json::array();
    for (const poco::Project &project : projects_) {
        rows.push_back({{"id", project.id}, {"name", project.name}});
    }
    catalog_->set_value(rows); ///< The combo reads this attribute, not the vector.
}

// ---------------------------------------------------------------------------
// Pages. The worker mail is accepted here, then handed to the list.
// ---------------------------------------------------------------------------

caf::message_handler ProjectSelector::message_handler_extensions() {
    auto apply = [this](std::uint64_t requestId, bool append, const utility::JsonStore &payload) {
        if (page_) {
            page_(requestId, append, payload); ///< The lambda from the latest loadProjectsAsync.
        }
    };
    caf::detail::behavior_impl::pointer handler =
        caf::make_counted<ProjectPageHandler>(std::move(apply));
    return caf::message_handler{std::move(handler)};
}

} // namespace xstudio::mikros_shotbrowser
