// SPDX-License-Identifier: Apache-2.0
/**
 * @file session.cpp
 * @brief Stores the schema and the non-owning backend pointer.
 */

#include "pms/session.hpp"

#include "pms/backend.hpp"

#include <utility>

namespace pms {

Session::Session(Schema schema, Backend &backend)
    : schema_(std::move(schema)), backend_(&backend) {}

} // namespace pms
