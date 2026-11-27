// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_notes.cpp
 * @brief Notes screen: stamp, CAF worker load, NotesJson.
 */

#include "viewModel/version_notes.hpp"

#include "model/dal/dal.hpp"
#include "model/log.hpp"
#include "utility/media_meta.hpp"
#include "utility/notes_load.hpp"
#include "utility/status_update.hpp"

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
    status_default_color_ =
        panel.add_string_attribute("StatusDefaultColor", "StatusDefaultColor", "");
    status_default_color_->expose_in_ui_attrs_group(kAttrsGroup);
    note_status_editable_ =
        panel.add_boolean_attribute("NoteStatusEditable", "NoteStatusEditable", false);
    note_status_editable_->expose_in_ui_attrs_group(kAttrsGroup);
    note_status_edit_ =
        panel.add_json_attribute("NoteStatusEdit", "NoteStatusEdit", nlohmann::json::object());
    note_status_edit_->expose_in_ui_attrs_group(kAttrsGroup);
    note_status_error_ =
        panel.add_string_attribute("NoteStatusEditError", "NoteStatusEditError", "");
    note_status_error_->expose_in_ui_attrs_group(kAttrsGroup);
    note_status_busy_ = panel.add_boolean_attribute("NoteStatusEditBusy", "NoteStatusEditBusy", false);
    note_status_busy_->expose_in_ui_attrs_group(kAttrsGroup);
}

void VersionNotes::publishStatusStyles() {
    status_colors_->set_value(styleJson(dal::noteStatusColors()));
    status_labels_->set_value(styleJson(dal::noteStatusLabels()));
    status_default_color_->set_value(dal::noteStatusDefaultColor()); ///< Unknown note codes.
    note_status_editable_->set_value(dal::statusStyle("note", "status").editable);
}

void VersionNotes::attribute_changed(const utility::Uuid &attr_uuid, int role) {
    if (role != module::Attribute::Value) {
        return; ///< Only a new value is a panel edit this screen handles.
    }
    if (note_status_edit_ && attr_uuid == note_status_edit_->uuid()) {
        submitStatusEdit(note_status_edit_->value());
    }
}

void VersionNotes::submitStatusEdit(const nlohmann::json &edit) {
    if (!edit.is_object() || !edit.contains("entity")) {
        return; ///< The clear before a repeat choice is not a write.
    }
    note_status_error_->set_value("");
    note_status_busy_->set_value(true);
    StatusUpdateRequest request;
    request.panel = caf::actor_cast<caf::weak_actor_ptr>(panelActor(panel_));
    request.request_id = generation_.load(); ///< A media change bumps this and drops the result.
    request.entity = edit.value("entity", std::string{});
    request.id = edit.value("id", std::int64_t{0});
    request.field = edit.value("field", std::string{});
    request.code = edit.value("code", std::string{});
    request.catalog_ok = "note_status_updated";
    request.catalog_error = "note_status_error";
    launchStatusUpdate(panel_.system(), std::move(request));
}

void VersionNotes::applyNoteStatus(const nlohmann::json &payload) {
    if (!notes_json_) {
        return;
    }
    nlohmann::json json = notes_json_->value();
    if (!json.contains("notes") || !json["notes"].is_array()) {
        return;
    }
    const std::int64_t id = payload.value("id", std::int64_t{0});
    const std::string code = payload.value("code", std::string{});
    for (nlohmann::json &note : json["notes"]) {
        if (note.value("id", std::int64_t{0}) == id) {
            note["status"] = code;
        }
    }
    notes_json_->set_value(std::move(json)); ///< NotesState stays ready.
}

void VersionNotes::sendNotesToQml(
    const nlohmann::json &notes, const std::string &state, const std::string &error) {
    notes_json_->set_value(notes);
    notes_state_->set_value(state);
    notes_error_->set_value(error);
}

void VersionNotes::clearNotes() {
    sendNotesToQml(nlohmann::json::object(), "empty");
    if (note_status_error_) {
        note_status_error_->set_value("");
    }
    if (note_status_busy_) {
        note_status_busy_->set_value(false); ///< An in-flight write for the previous media is dropped.
    }
}

void VersionNotes::showNotMikros() {
    sendNotesToQml(nlohmann::json::object(), "not_mikros");
}

void VersionNotes::loadNotesFromIdAsync(std::int64_t versionId, std::uint64_t generation) {
    NotesLoadRequest request;
    request.panel = caf::actor_cast<caf::weak_actor_ptr>(panelActor(panel_));
    request.request_id = generation;
    request.version_id = versionId;
    launchNotesLoad(panel_.system(), std::move(request)); ///< Token mail, not a new CAF type.
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
                log::info(
                    "ui load notes version=" +
                    std::to_string(*versionId)); ///< The notes find follows this media change.
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
    if (catalog == "note_status_updated") {
        note_status_busy_->set_value(false);
        applyNoteStatus(payload);
        return;
    }
    if (catalog == "note_status_error") {
        note_status_busy_->set_value(false);
        note_status_error_->set_value(payload.value("message", std::string{"Update failed"}));
        return; ///< The badge keeps the previous code.
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
