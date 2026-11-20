// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/constants.cpp
 * @brief Reads the Shot Browser catalog page size from the environment.
 */

#include "model/constants.hpp"

#include "pms/error.hpp"

#include <cerrno>
#include <cstdlib>

namespace xstudio::mikros_shotbrowser::constants {
namespace {

/**
 * @brief Reads one environment variable.
 * @param name Variable name.
 * @return The text, or empty when the variable is absent or empty.
 */
std::string readEnv(const char *name) {
    const char *text = std::getenv(name);
    if (!text || text[0] == '\0') {
        return {};
    }
    return text;
}

/**
 * @brief Resolves one positive integer setting.
 * @param name Variable name.
 * @param fallback Value used when `name` is absent or empty.
 * @return The environment value when set, otherwise `fallback`.
 * @throw pms::Error When the variable is set but not an integer greater than zero.
 */
NumberSetting numberSetting(const char *name, std::uint64_t fallback) {
    const std::string env = readEnv(name);
    if (env.empty()) {
        return NumberSetting{fallback, false};
    }
    char *end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(env.c_str(), &end, 10);
    if (end == env.c_str() || *end != '\0' || errno == ERANGE || value == 0) {
        throw pms::Error(std::string(name) + " must be an integer greater than zero");
    }
    return NumberSetting{static_cast<std::uint64_t>(value), true};
}

} // namespace

NumberSetting pageSize() {
    const NumberSetting setting = numberSetting(kPageSize, kPageSizeDefault);
    if (setting.value > kPageSizeMax) {
        throw pms::Error(std::string(kPageSize) + " must be at most 500");
    }
    return setting;
}

} // namespace xstudio::mikros_shotbrowser::constants
