// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/project_selector.cpp
 * @brief Project combo: page load, selected id, and status/type filters.
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
#include <set>
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
 * @brief Reads the string array a filter attribute stores.
 *
 * A JSON string is parsed first, so a QML write of either an array or its
 * text form is accepted. Empty strings are not checks.
 * @param attribute Shown statuses or shown types.
 * @return The checked values, or empty when the attribute is missing.
 */
std::set<std::string> shownValuesFromAttribute(const module::JsonAttribute *attribute) {
    std::set<std::string> values;
    if (!attribute) {
        return values;
    }
    nlohmann::json json = attribute->value();
    if (json.is_string()) {
        try {
            json = nlohmann::json::parse(json.get<std::string>()); ///< QML wrote the array as text.
        } catch (const std::exception &) {
            return values;
        }
    }
    if (!json.is_array()) {
        return values;
    }
    for (const auto &entry : json) {
        if (!entry.is_string()) {
            continue;
        }
        const std::string value = entry.get<std::string>();
        if (!value.empty()) {
            values.insert(value); ///< A blank ShotGrid value is not a check.
        }
    }
    return values;
}

/**
 * @brief Writes the distinct non-empty values of one project field.
 * @tparam Field Callable that reads one string from a project.
 * @param attribute Option list the menu reads.
 * @param projects Loaded projects.
 * @param field Status or type accessor.
 */
template <typename Field>
void publishDistinctValues(
    module::JsonAttribute *attribute,
    const std::vector<poco::Project> &projects,
    Field field) {
    if (!attribute) {
        return;
    }
    std::set<std::string> values;
    for (const poco::Project &project : projects) {
        const std::string value = field(project);
        if (!value.empty()) {
            values.insert(value); ///< Empty ShotGrid values are not menu rows.
        }
    }
    nlohmann::json rows = nlohmann::json::array();
    for (const std::string &value : values) {
        rows.push_back(value);
    }
    attribute->set_value(rows); ///< The menu lists these strings.
}

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
    setDefaultValues(); ///< Schema `show` lists become the first checked filters.
}

void ProjectSelector::initQmlAttributes(plugin::StandardPlugin &panel) {
    catalog_ = panel.add_json_attribute("Projects", "Projects", nlohmann::json::array());
    catalog_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    selected_ = panel.add_string_attribute("SelectedProject", "SelectedProject", "-1");
    selected_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    status_options_ = panel.add_json_attribute(
        "ProjectStatusOptions", "ProjectStatusOptions", nlohmann::json::array());
    status_options_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    type_options_ = panel.add_json_attribute(
        "ProjectTypeOptions", "ProjectTypeOptions", nlohmann::json::array());
    type_options_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    shown_statuses_attribute_ =
        panel.add_json_attribute("ShownStatuses", "ShownStatuses", nlohmann::json::array());
    shown_statuses_attribute_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    shown_types_attribute_ =
        panel.add_json_attribute("ShownTypes", "ShownTypes", nlohmann::json::array());
    shown_types_attribute_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
}

void ProjectSelector::setDefaultValues() {
    setDefaultProjectShown();
    setDefaultProjectType();
}

void ProjectSelector::setDefaultProjectShown() {
    const std::vector<std::string> statuses = dal::defaultShownProjectStatuses();
    shown_statuses_ = {statuses.begin(), statuses.end()}; ///< Schema `show` on status.
    nlohmann::json statusJson = nlohmann::json::array();
    for (const std::string &value : statuses) {
        statusJson.push_back(value);
    }
    if (shown_statuses_attribute_) {
        shown_statuses_attribute_->set_value(statusJson); ///< Menu checks these statuses.
    }
}

void ProjectSelector::setDefaultProjectType() {
    const std::vector<std::string> types = dal::defaultShownProjectTypes();
    shown_types_ = {types.begin(), types.end()}; ///< Schema `show` on type.
    nlohmann::json typeJson = nlohmann::json::array();
    for (const std::string &value : types) {
        typeJson.push_back(value);
    }
    if (shown_types_attribute_) {
        shown_types_attribute_->set_value(typeJson); ///< Menu checks these types.
    }
}

// ---------------------------------------------------------------------------
// Selection. The combo writes an id. These functions store it and send it back.
// ---------------------------------------------------------------------------

void ProjectSelector::attribute_changed(const utility::Uuid &attr_uuid, int role) {
    if (role != module::Attribute::Value) {
        return; ///< Only a new value is a choice.
    }
    if (selected_ && attr_uuid == selected_->uuid()) {
        const std::int64_t projectId = readSelectedProjectFromQml();
        if (projectId == dal::selectedProject()) {
            return; ///< The QML write repeats an id already stored.
        }
        setSelectedProject(projectId);
        return;
    }
    if (isShownFilter(attr_uuid)) {
        applyShownFilters(); ///< Checked values become the combo contents.
    }
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
    // First project catalog of this panel session.
    trace::info("ui open shot browser");
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
    publishFilterOptions(); ///< The menu has no choices until the next page.
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
    publishFilterOptions(); ///< The menu grows as later pages add values.
    sendProjectsListToQml();
}

void ProjectSelector::sortProjectsByName() {
    std::sort(projects_.begin(), projects_.end(), projectNameAlphabeticOrderAlgorithm);
}

void ProjectSelector::sendProjectsListToQml() {
    nlohmann::json rows = nlohmann::json::array();
    for (const poco::Project &project : projects_) {
        if (!projectPassesFilters(project)) {
            continue; ///< A checked status or type hides this project.
        }
        rows.push_back({{"id", project.id}, {"name", project.name}});
    }
    catalog_->set_value(rows); ///< The combo reads this attribute, not the vector.
}

bool ProjectSelector::isShownFilter(const utility::Uuid &attr_uuid) const {
    const bool status = shown_statuses_attribute_ && attr_uuid == shown_statuses_attribute_->uuid();
    const bool type = shown_types_attribute_ && attr_uuid == shown_types_attribute_->uuid();
    return status || type;
}

void ProjectSelector::applyShownFilters() {
    const std::set<std::string> statuses = shownValuesFromAttribute(shown_statuses_attribute_);
    const std::set<std::string> types = shownValuesFromAttribute(shown_types_attribute_);
    if (statuses == shown_statuses_ && types == shown_types_) {
        return; ///< The QML write repeats the checks already stored.
    }
    shown_statuses_ = statuses;
    shown_types_ = types;
    sendProjectsListToQml();
    clearSelectionWhenHidden(); ///< A hidden project must not stay selected.
}

bool ProjectSelector::projectPassesFilters(const poco::Project &project) const {
    const bool statusHidden = !shown_statuses_.empty() &&
                              shown_statuses_.find(project.status) == shown_statuses_.end();
    if (statusHidden) {
        return false; ///< A checked status hides every other status.
    }
    const bool typeHidden =
        !shown_types_.empty() && shown_types_.find(project.type) == shown_types_.end();
    if (typeHidden) {
        return false; ///< A checked type hides every other type.
    }
    return true;
}

void ProjectSelector::clearSelectionWhenHidden() {
    const std::int64_t projectId = dal::selectedProject();
    if (projectId == -1) {
        return; ///< None stays None.
    }
    for (const poco::Project &project : projects_) {
        if (project.id != projectId) {
            continue;
        }
        if (!projectPassesFilters(project)) {
            // A hidden project is cleared and the catalogs reload.
            trace::info("ui filter projects");
            clearProjectSelected(); ///< The trees follow None.
        }
        return;
    }
}

void ProjectSelector::publishFilterOptions() {
    publishDistinctValues(
        status_options_, projects_, [](const poco::Project &project) { return project.status; });
    publishDistinctValues(
        type_options_, projects_, [](const poco::Project &project) { return project.type; });
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
