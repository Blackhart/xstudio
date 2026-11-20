// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/env.hpp
 * @brief Process environment for the shared pms session and cache.
 */
#pragma once

#include <cstdint>
#include <string>

namespace pms {

/**
 * @brief A text setting and whether the environment supplied it.
 */
struct TextSetting {
    std::string value; ///< Environment value, or the default. Empty when there is no default.
    bool set = false;  ///< True when the environment variable is non-empty.
};

/**
 * @brief A numeric setting and whether the environment supplied it.
 */
struct NumberSetting {
    std::uint64_t value = 0; ///< Environment value, or the default.
    bool set = false;        ///< True when the environment variable is non-empty.
};

inline constexpr const char *kBackend = "MIKROS_PMS_BACKEND";
inline constexpr const char *kBackendDefault = "fake";
inline constexpr const char *kCacheTtlSeconds = "MIKROS_PMS_CACHE_TTL_SECONDS";
inline constexpr std::uint64_t kCacheTtlSecondsDefault = 300;
inline constexpr const char *kCacheBytes = "MIKROS_PMS_CACHE_BYTES";
inline constexpr std::uint64_t kCacheBytesDefault = 64 * 1024 * 1024;
inline constexpr const char *kSchemaPath = "PMS_SCHEMA_PATH";
inline constexpr const char *kShotGridUrl = "SHOTGRID_URL";
inline constexpr const char *kShotGridClientId = "SHOTGRID_CLIENT_ID";
inline constexpr const char *kShotGridClientSecret = "SHOTGRID_CLIENT_SECRET";
inline constexpr const char *kShotGridMapping = "SHOTGRID_MAPPING";

/**
 * @brief Backend name.
 * @return `shotgrid` when set, otherwise `fake`.
 */
TextSetting backend();

/**
 * @brief Lifetime of one cached page, in seconds.
 * @return The environment value, or 300.
 * @throw Error When the variable is set but not an integer greater than zero.
 */
NumberSetting cacheTtlSeconds();

/**
 * @brief Byte budget of the page cache.
 * @return The environment value, or 64 MiB.
 * @throw Error When the variable is set but not an integer greater than zero.
 */
NumberSetting cacheMaxBytes();

/**
 * @brief Path of schema.yaml.
 * @return `PMS_SCHEMA_PATH` when set, otherwise empty with `set` false.
 */
TextSetting schemaPath();

/**
 * @brief ShotGrid site URL. No default.
 * @return The environment value, or empty with `set` false.
 */
TextSetting shotGridUrl();

/**
 * @brief ShotGrid script name. No default.
 * @return The environment value, or empty with `set` false.
 */
TextSetting shotGridClientId();

/**
 * @brief ShotGrid script key. No default. The value is the real key.
 * @return The environment value, or empty with `set` false.
 */
TextSetting shotGridClientSecret();

/**
 * @brief Path of the studio ShotGrid mapping. No default.
 * @return The environment value, or empty with `set` false.
 */
TextSetting shotGridMapping();

/**
 * @brief Shows enough of a script key to recognise it, never the whole secret.
 * @param key `SHOTGRID_CLIENT_SECRET`.
 * @return `abcd...wxyz length=N`, or only the length when the key is short.
 */
std::string keyHint(const std::string &key);

} // namespace pms
