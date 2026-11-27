// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/version_notes.hpp
 * @brief Notes screen: playhead media → stamp → CAF worker → NotesJson.
 */
#pragma once

#include "xstudio/plugin_manager/plugin_base.hpp"
#include "xstudio/utility/json_store.hpp"
#include "xstudio/utility/media_reference.hpp"

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
     * @brief Publishes the note status colour, label, and default colour once.
     */
    void publishStatusStyles();

    plugin::StandardPlugin &panel_; ///< Actor that owns attributes and receives pages.
    module::StringAttribute *notes_state_ = nullptr; ///< empty / not_mikros / loading / ready / error.
    module::StringAttribute *notes_error_ = nullptr; ///< Error text when state is error.
    module::JsonAttribute *notes_json_ = nullptr;    ///< `{notes: [...]}`.
    module::JsonAttribute *status_colors_ = nullptr; ///< Code → colour from the schema.
    module::JsonAttribute *status_labels_ = nullptr; ///< Code → accessible name from the schema.
    module::StringAttribute *status_default_color_ = nullptr; ///< Colour for an unknown note code.
    std::atomic<std::uint64_t> generation_{0}; ///< Bumped on each on_screen_media_changed.
};

} // namespace xstudio::mikros_media_details
