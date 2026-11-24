// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/shelf_store.hpp
 * @brief In-memory user shelves loaded and saved through preferences.
 */
#pragma once

#include "model/poco/shelf.hpp"

#include "xstudio/plugin_manager/plugin_base.hpp"

#include <optional>
#include <string>
#include <vector>

namespace xstudio::mikros_shotbrowser {

/**
 * @brief User shelves persisted under the shotbrowser preference path.
 *
 * The virtual All shelf is not stored. Studio shelves are out of scope.
 */
class ShelfStore {
  public:
    /**
     * @brief Preference path for the shelves JSON array.
     * @return Absolute preference path.
     */
    static const char *preferencePath();

    /**
     * @brief Id of the virtual All shelf.
     * @return `"all"`.
     */
    static const char *allShelfId();

    /**
     * @brief Builds the virtual All shelf.
     * @return Shelf with an empty filters object.
     */
    static poco::Shelf allShelf();

    /**
     * @brief Loads user shelves from the panel preferences.
     * @param panel Panel whose actor system owns the global store.
     */
    void load(plugin::StandardPlugin &panel);

    /**
     * @brief Writes user shelves to the panel preferences.
     * @param panel Panel whose actor system owns the global store.
     */
    void save(plugin::StandardPlugin &panel) const;

    /**
     * @brief User shelves currently in memory.
     * @return Shelves in display order. Does not include All.
     */
    const std::vector<poco::Shelf> &shelves() const { return shelves_; }

    /**
     * @brief Inserts or replaces one shelf by id.
     * @param shelf Shelf to keep. Id `"new"` becomes a fresh uuid. Id `"all"` is ignored.
     * @return The stored shelf id, or empty when ignored.
     */
    std::string upsert(poco::Shelf shelf);

    /**
     * @brief Removes one user shelf by id.
     * @param id Shelf id. `"all"` is ignored.
     */
    void remove(const std::string &id);

    /**
     * @brief Moves one user shelf to a new index for display order.
     * @param id Shelf id. `"all"` ignored.
     * @param toIndex Destination in `[0, size)`. Clamped. No-op when unchanged or missing.
     * @return True when the vector order changed.
     */
    bool moveTo(const std::string &id, std::size_t toIndex);

    /**
     * @brief Finds a user shelf by id.
     * @param id Shelf id.
     * @return The shelf, or empty when missing.
     */
    std::optional<poco::Shelf> findById(const std::string &id) const;

    /**
     * @brief Id of the shelf currently selected in the list.
     * @return Selected id, or empty when none.
     */
    const std::string &activeId() const { return active_id_; }

    /**
     * @brief Remembers which shelf is selected.
     * @param id Shelf id, `"all"`, or empty to clear.
     */
    void setActiveId(std::string id);

  private:
    std::vector<poco::Shelf> shelves_; ///< User shelves only.
    std::string active_id_;            ///< Selected shelf id, or empty.
};

} // namespace xstudio::mikros_shotbrowser
