// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_notes.hpp
 * @brief Notes screen: playhead media → stamp → CAF worker → NotesJson.
 */
#pragma once

#include "xstudio/plugin_manager/plugin_base.hpp"
#include "xstudio/utility/json_store.hpp"
#include "xstudio/utility/media_reference.hpp"

#include <nlohmann/json.hpp>

#include <QObject>

#include <atomic>
#include <cstdint>
#include <string>

namespace xstudio::mikros_media_details {

/**
 * @brief View model of the Notes tab.
 *
 * It reads the Mikros stamp itself. It does not read Media Details fields.
 * A failed notes load does not change DetailState.
 */
class VersionNotes : public QObject {
    Q_OBJECT

  public:
    /**
     * @brief Publishes the empty notes attributes and the status colours.
     * @param panel Panel actor that owns the attributes.
     */
    explicit VersionNotes(plugin::StandardPlugin &panel);

    /**
     * @brief Drops the screen. In-flight loads are ignored.
     */
    ~VersionNotes() override;

    /**
     * @brief Reloads notes from the on-screen media.
     *
     * Clears QML, bumps this screen's generation, then reads the stamp.
     * Missing media clears the list. No Mikros stamp shows `not_mikros`.
     * @param media On-screen media actor, or null.
     * @param media_ref Path and frame data. Unused; the stamp is on the actor.
     */
    void on_screen_media_changed(caf::actor media, const utility::MediaReference &media_ref);

    /**
     * @brief Handles a panel attribute change owned by this screen.
     *
     * Ignores non-Value roles. NoteStatusEdit starts one status write.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

    /**
     * @brief Applies a worker page when the generation still matches.
     * @param generation Request id from the worker.
     * @param catalog `version_notes` or `version_notes_error`.
     * @param payload Notes JSON, or `{message}`.
     */
    void applyWorkerPage(
        std::uint64_t generation, const std::string &catalog, const utility::JsonStore &payload);

  private:
    /**
     * @brief Spawns the CAF worker that loads notes for one version.
     *
     * Does not bump `generation_`: the playhead handler already did that.
     * @param versionId ShotGrid version id from the media stamp.
     * @param generation Request id from `on_screen_media_changed`.
     */
    void loadNotesFromIdAsync(std::int64_t versionId, std::uint64_t generation);

    /**
     * @brief Clears NotesJson / NotesState / NotesError. Does not bump generation.
     */
    void clearNotes();

    /**
     * @brief Publishes NotesJson, NotesState, and NotesError together.
     * @param notes `{notes: [...]}`, or an empty object.
     * @param state `empty`, `not_mikros`, `loading`, `ready`, or `error`.
     * @param error Error text when state is `error`, otherwise empty.
     */
    void sendNotesToQml(
        const nlohmann::json &notes, const std::string &state, const std::string &error = {});

    /**
     * @brief Publishes `not_mikros` and drops any previous list.
     */
    void showNotMikros();

    /**
     * @brief Creates NotesState, NotesError, NotesJson, and the note status style attributes.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

    /**
     * @brief Publishes the note status colour, label, default colour, and editable flag once.
     */
    void publishStatusStyles();

    /**
     * @brief Starts a note status write when QML sends an id and a code.
     * @param edit JSON object. An object without `entity` is the clear before a repeat choice.
     */
    void submitStatusEdit(const nlohmann::json &edit);

    /**
     * @brief Writes the confirmed code onto the matching note.
     * @param payload `{entity, id, field, code}` from the worker.
     */
    void applyNoteStatus(const nlohmann::json &payload);

    plugin::StandardPlugin &panel_; ///< Actor that owns attributes and receives pages.
    module::StringAttribute *notes_state_ = nullptr; ///< empty / not_mikros / loading / ready / error.
    module::StringAttribute *notes_error_ = nullptr; ///< Error text when state is error.
    module::JsonAttribute *notes_json_ = nullptr;    ///< `{notes: [...]}`.
    module::JsonAttribute *status_colors_ = nullptr; ///< Code → colour from the schema.
    module::JsonAttribute *status_labels_ = nullptr; ///< Code → accessible name from the schema.
    module::StringAttribute *status_default_color_ = nullptr; ///< Colour for an unknown note code.
    module::BooleanAttribute *note_status_editable_ = nullptr; ///< True when note.status is editable.
    module::JsonAttribute *note_status_edit_ = nullptr; ///< `{entity,id,field,code}` QML writes.
    module::StringAttribute *note_status_error_ = nullptr; ///< Update error. Empty after a new choice.
    module::BooleanAttribute *note_status_busy_ = nullptr; ///< True while a note status write is in flight.
    std::atomic<std::uint64_t> generation_{0}; ///< Bumped on each on_screen_media_changed.
};

} // namespace xstudio::mikros_media_details
