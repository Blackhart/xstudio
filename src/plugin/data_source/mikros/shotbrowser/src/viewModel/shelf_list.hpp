// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/shelf_list.hpp
 * @brief Shelf list: Studio/User rows, selection, and editor CRUD.
 */
#pragma once

#include "model/shelf_store.hpp"

using namespace xstudio;

#include <QObject>

#include <string>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief View model of the named multi-entity searches (shelves).
 *
 * Publishes a JSON tree for Studio / User. Selection writes the filters into
 * the DAL and emits `onShelfSelected`. Upsert and remove persist through
 * `ShelfStore`.
 */
class ShelfList : public QObject {
    Q_OBJECT

  public:
    /**
     * @brief Loads preferences and publishes the empty shelf attributes.
     * @param panel Panel actor that owns attributes and the preference store.
     */
    explicit ShelfList(plugin::StandardPlugin &panel);

    /**
     * @brief Applies selection, upsert, or remove.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value is a request.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

    /**
     * @brief Rebuilds the Shelves JSON from the store (All + user rows).
     */
    void publishShelves();

    /**
     * @brief Clears the selected shelf in the DAL and in the attribute.
     */
    void clearSelection();

    /**
     * @brief Selects the virtual All shelf and publishes SelectedShelf.
     *
     * Used at construction and when entering Shelf mode.
     */
    void selectAllByDefault();

  signals:
    /**
     * @brief Emitted after the active shelf filters are stored in the DAL.
     */
    void onShelfSelected();

  private:
    /**
     * @brief Publishes Shelves, SelectedShelf, ShelfFields, UpsertShelf, RemoveShelf, MoveShelf.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

    /**
     * @brief Stores the selected shelf filters in the DAL and emits.
     * @param shelfId Shelf id, including `"all"`.
     */
    void selectShelf(const std::string &shelfId);

    /**
     * @brief Parses UpsertShelf JSON, saves, and republishes.
     * @param payload Shelf object from QML.
     */
    void upsertFromJson(const nlohmann::json &payload);

    /**
     * @brief Removes one user shelf and republishes.
     * @param shelfId Shelf id to drop.
     */
    void removeShelf(const std::string &shelfId);

    /**
     * @brief Moves one user shelf in the list and republishes.
     * @param shelfId Shelf id to move.
     * @param toIndex Destination index among user shelves.
     */
    void moveShelf(const std::string &shelfId, std::size_t toIndex);

    /**
     * @brief Applies MoveShelf JSON from QML.
     * @param payload Object with `id` and `toIndex`.
     */
    void moveFromJson(const nlohmann::json &payload);

    /**
     * @brief Publishes filterable fields for every entity except project.
     */
    void publishShelfFields();

    plugin::StandardPlugin &panel_; ///< Panel that owns attributes and prefs.
    ShelfStore store_;              ///< User shelves in memory and on disk.
    module::JsonAttribute *shelves_ = nullptr;        ///< Studio/User tree for QML.
    module::StringAttribute *selected_shelf_ = nullptr; ///< Selected shelf id.
    module::JsonAttribute *shelf_fields_ = nullptr;   ///< Filterable fields for the editor.
    module::JsonAttribute *upsert_shelf_ = nullptr;   ///< Write a shelf object to create/update.
    module::StringAttribute *remove_shelf_ = nullptr; ///< Write an id to delete.
    module::JsonAttribute *move_shelf_ = nullptr;     ///< Write {id, toIndex} to reorder.
};

} // namespace xstudio::mikros_shotbrowser
