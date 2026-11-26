// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_notes.cpp
 * @brief Notes screen: stamp, CAF worker load, NotesJson.
 */

#include "viewModel/version_notes.hpp"

#include "model/dal/dal.hpp"
#include "utility/media_meta.hpp"
#include "utility/notes_load.hpp"

#include "xstudio/atoms.hpp"
#include "xstudio/utility/helpers.hpp"

#include <caf/actor_cast.hpp>
#include <caf/actor_system.hpp>
#include <caf/event_based_actor.hpp>

#include <map>
#include <optional>
#include <utility>

namespace xstudio::mikros_media_details {
namespace {

constexpr const char *kAttrsGroup = "mikros_version_notes_attributes";

/**
 * @brief Panel actor pointer from the StandardPlugin reference.
 * @param panel Plugin actor that owns the screen.
 * @return Strong actor handle for the panel.
 */
caf::actor panelActor(plugin::StandardPlugin &panel) {
    return caf::actor_cast<caf::actor>(static_cast<caf::event_based_actor *>(&panel));
}

/**
 * @brief Turns a code map into a JSON object the list can look up.
 * @param values Code → colour, or code → label.
 * @return Object with one key per code.
 */
nlohmann::json styleJson(const std::map<std::string, std::string> &values) {
    nlohmann::json object = nlohmann::json::object();
    for (const auto &entry : values) {
        object[entry.first] = entry.second;
    }
    return object;
}

} // namespace

VersionNotes::VersionNotes(plugin::StandardPlugin &panel) : QObject(nullptr), panel_(panel) {
    registerNotesLoadType(); ///< Before the first detached load sends a request.
    initQmlAttributes(panel);
    publishStatusStyles();
}

VersionNotes::~VersionNotes() = default;

void VersionNotes::initQmlAttributes(plugin::StandardPlugin &panel) {
    notes_state_ = panel.add_string_attribute("NotesState", "NotesState", "empty");
    notes_state_->expose_in_ui_attrs_group(kAttrsGroup);
    notes_error_ = panel.add_string_attribute("NotesError", "NotesError", "");
    notes_error_->expose_in_ui_attrs_group(kAttrsGroup);
    notes_json_ = panel.add_json_attribute("NotesJson", "NotesJson", nlohmann::json::object());
    notes_json_->expose_in_ui_attrs_group(kAttrsGroup);
    status_colors_ =
        panel.add_json_attribute("StatusColors", "StatusColors", nlohmann::json::object());
    status_colors_->expose_in_ui_attrs_group(kAttrsGroup);
    status_labels_ =
        panel.add_json_attribute("StatusLabels", "StatusLabels", nlohmann::json::object());
    status_labels_->expose_in_ui_attrs_group(kAttrsGroup);
}

void VersionNotes::publishStatusStyles() {
    status_colors_->set_value(styleJson(dal::noteStatusColors()));
    status_labels_->set_value(styleJson(dal::noteStatusLabels()));
}

void VersionNotes::sendNotesToQml(
    const nlohmann::json &notes, const std::string &state, const std::string &error) {
    notes_json_->set_value(notes);
    notes_state_->set_value(state);
    notes_error_->set_value(error);
}

void VersionNotes::clearNotes() {
    sendNotesToQml(nlohmann::json::object(), "empty");
}

void VersionNotes::showNotMikros() {
    sendNotesToQml(nlohmann::json::object(), "not_mikros");
}

void VersionNotes::loadNotesFromIdAsync(std::int64_t versionId, std::uint64_t generation) {
    NotesLoadRequest request;
    request.panel = caf::actor_cast<caf::weak_actor_ptr>(panelActor(panel_));
    request.request_id = generation;
    request.version_id = versionId;
    auto worker = panel_.system().spawn<NotesLoadActor, caf::detached>();
    anon_mail(std::move(request)).send(worker);
}

void VersionNotes::on_screen_media_changed(
    caf::actor media, const utility::MediaReference & /*media_ref*/) {
    // Invalidate in-flight loads first, then clear QML (clearNotes does not bump).
    const std::uint64_t generation = generation_.fetch_add(1) + 1;
    clearNotes();

    if (!media) {
        return;
    }

    sendNotesToQml(nlohmann::json::object(), "loading");

    panel_
        .mail(json_store::get_json_atom_v, utility::Uuid(), std::string(kMikrosVersionMetaPath))
        .request(media, caf::infinite)
        .then(
            [=](const utility::JsonStore &meta) {
                if (generation != generation_.load()) {
                    return; ///< A newer selection already replaced this load.
                }
                const std::optional<std::int64_t> versionId = versionIdFromStamp(meta);
                if (!versionId) {
                    showNotMikros();
                    return;
                }
                loadNotesFromIdAsync(*versionId, generation);
            },
            [=](const caf::error &) {
                if (generation != generation_.load()) {
                    return;
                }
                showNotMikros();
            });
}

void VersionNotes::applyWorkerPage(
    std::uint64_t generation, const std::string &catalog, const utility::JsonStore &payload) {
    if (generation != generation_.load()) {
        return; ///< A newer selection already replaced this load.
    }
    if (catalog == "version_notes_error") {
        sendNotesToQml(
            nlohmann::json::object(),
            "error",
            payload.value("message", std::string{"Load failed"}));
        return;
    }
    if (catalog != "version_notes") {
        return;
    }
    sendNotesToQml(payload, "ready");
}

} // namespace xstudio::mikros_media_details
