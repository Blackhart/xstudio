// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/error.hpp
 * @brief Error type used by the query library.
 */
#pragma once

#include <stdexcept>
#include <string>

namespace pms {

/**
 * @brief Failure raised by validation, parsing, or a backend result.
 *
 * The message names the clause (`where`) and the key when both exist.
 * Callers do not swallow it.
 */
class Error : public std::runtime_error {
  public:
    /**
     * @brief Builds an error from a complete message.
     * @param message Text stored by `std::runtime_error`.
     */
    explicit Error(const std::string &message) : std::runtime_error(message) {}
};

} // namespace pms
