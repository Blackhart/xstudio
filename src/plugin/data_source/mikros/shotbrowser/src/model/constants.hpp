// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/constants.hpp
 * @brief Shot Browser environment variables and their defaults.
 */
#pragma once

#include <cstdint>
#include <string>

namespace xstudio::mikros_shotbrowser::constants {

inline constexpr const char *kBackend = "MIKROS_PMS_BACKEND";
inline constexpr const char *kBackendDefault = "fake";
inline constexpr const char *kCacheTtlSeconds = "MIKROS_PMS_CACHE_TTL_SECONDS";
inline constexpr std::uint64_t kCacheTtlSecondsDefault = 300;
inline constexpr const char *kCacheBytes = "MIKROS_PMS_CACHE_BYTES";
inline constexpr std::uint64_t kCacheBytesDefault = 64 * 1024 * 1024;
inline constexpr const char *kPageSize = "MIKROS_PMS_PAGE_SIZE";
inline constexpr std::uint64_t kPageSizeDefault = 50;
inline constexpr std::uint64_t kPageSizeMax = 500;
inline constexpr const char *kSchemaPath = "PMS_SCHEMA_PATH";
inline constexpr const char *kShotGridUrl = "SHOTGRID_URL";
inline constexpr const char *kShotGridClientId = "SHOTGRID_CLIENT_ID";
inline constexpr const char *kShotGridClientSecret = "SHOTGRID_CLIENT_SECRET";
inline constexpr const char *kShotGridMapping = "SHOTGRID_MAPPING";

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

/**
 * @brief Backend name.
 * @return `shotgrid` when set, otherwise `fake`.
 */
TextSetting backend();

/**
 * @brief Lifetime of one cached page, in seconds.
 * @return The environment value, or 300.
 * @throw pms::Error When the variable is set but not an integer greater than zero.
 */
NumberSetting cacheTtlSeconds();

/**
 * @brief Byte budget of the page cache.
 * @return The environment value, or 64 MiB.
 * @throw pms::Error When the variable is set but not an integer greater than zero.
 */
NumberSetting cacheMaxBytes();

/**
 * @brief Rows requested in one catalog page.
 * @return The environment value, or 50.
 * @throw pms::Error When the variable is set but invalid, or greater than 500.
 */
NumberSetting pageSize();

/**
 * @brief Path of schema.yaml.
 * @return `PMS_SCHEMA_PATH`, or the copy installed under the xSTUDIO share directory.
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

} // namespace xstudio::mikros_shotbrowser::constants
