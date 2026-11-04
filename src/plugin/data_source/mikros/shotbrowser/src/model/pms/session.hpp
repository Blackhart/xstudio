// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/session.hpp
 * @brief Schema and backend passed to a verb.
 */
#pragma once

#include "pms/schema.hpp"

namespace pms {

class Backend;

/**
 * @brief Pair of a domain schema and the backend that executes a verb.
 *
 * This is not a user session, a login, or a ShotGrid connection. Build it once
 * when the tool starts and pass it to find(). The query chain does not see it
 * until the verb runs. Several sessions can exist at once.
 *
 * The session does not own the backend. The backend must outlive every find()
 * that uses this session.
 */
class Session {
  public:
    /**
     * @brief Binds a schema to a backend.
     * @param schema Domain schema, kept by value.
     * @param backend Execution target. Not owned.
     */
    Session(Schema schema, Backend &backend);

    /**
     * @brief Returns the schema used to validate queries.
     * @return The schema stored at construction.
     */
    const Schema &schema() const { return schema_; }

    /**
     * @brief Returns the backend that executes queries.
     * @return The backend passed at construction.
     */
    Backend &backend() const { return *backend_; }

  private:
    Schema schema_;    ///< Domain schema owned by the session.
    Backend *backend_; ///< Non-owning pointer to the execution target.
};

} // namespace pms
