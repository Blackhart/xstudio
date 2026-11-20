// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/env.cpp
 * @brief Reads each shared pms setting from the environment or its default.
 */

#include "pms/env.hpp"

#include "pms/error.hpp"

#include <cerrno>
#include <cstdlib>

namespace pms {
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
 * @brief Resolves one text setting.
 * @param name Variable name.
 * @param fallback Value used when `name` is absent or empty.
 * @return The environment value when set, otherwise `fallback`.
 */
TextSetting textSetting(const char *name, const std::string &fallback) {
    const std::string env = readEnv(name);
    if (env.empty()) {
        return TextSetting{fallback, false};
    }
    return TextSetting{env, true};
}

/**
 * @brief Resolves one positive integer setting.
 * @param name Variable name.
 * @param fallback Value used when `name` is absent or empty.
 * @return The environment value when set, otherwise `fallback`.
 * @throw Error When the variable is set but not an integer greater than zero.
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
        throw Error(std::string(name) + " must be an integer greater than zero");
    }
    return NumberSetting{static_cast<std::uint64_t>(value), true};
}

} // namespace

TextSetting backend() { return textSetting(kBackend, kBackendDefault); }

NumberSetting cacheTtlSeconds() { return numberSetting(kCacheTtlSeconds, kCacheTtlSecondsDefault); }

NumberSetting cacheMaxBytes() { return numberSetting(kCacheBytes, kCacheBytesDefault); }

TextSetting schemaPath() { return textSetting(kSchemaPath, {}); }

TextSetting shotGridUrl() { return textSetting(kShotGridUrl, {}); }

TextSetting shotGridClientId() { return textSetting(kShotGridClientId, {}); }

TextSetting shotGridClientSecret() { return textSetting(kShotGridClientSecret, {}); }

TextSetting shotGridMapping() { return textSetting(kShotGridMapping, {}); }

std::string keyHint(const std::string &key) {
    const std::string length = "length=" + std::to_string(key.size());
    if (key.size() <= 8) {
        return length;
    }
    return key.substr(0, 4) + "..." + key.substr(key.size() - 4) + " " + length;
}

} // namespace pms
