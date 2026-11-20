// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/shared_session.cpp
 * @brief Builds the process-wide Session and wires optional info traces.
 */

#include "pms/shared_session.hpp"

#include "pms/caching_backend.hpp"
#include "pms/env.hpp"
#include "pms/error.hpp"
#include "pms/ir.hpp"
#include "pms/page.hpp"
#include "pms/pms.hpp"
#include "pms/shotgrid/backend.hpp"
#include "pms/value.hpp"

#if defined(PMS_HAS_FAKE) && PMS_HAS_FAKE
#include "pms/fake/backend.hpp"
#endif

#include <chrono>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace pms {
namespace {

/**
 * @brief Process-wide info sink. Empty disables traces.
 * @return Mutable sink reference under the caller's care.
 */
std::function<void(std::string_view)> &infoSink() {
    static std::function<void(std::string_view)> sink;
    return sink;
}

/**
 * @brief Forwards one line to the installed sink when present.
 * @param message Trace body.
 */
void emitInfo(std::string_view message) {
    if (infoSink()) {
        infoSink()(message);
    }
}

/**
 * @brief Escapes a name so it can sit inside a quoted log field.
 * @param text Raw domain name.
 * @return Text with quotes and backslashes escaped.
 */
std::string escapeQuoted(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

/**
 * @brief Drops a trailing newline so the IR block does not end blank.
 * @param text dump() output.
 * @return The same text without a final newline.
 */
std::string trimNewline(std::string text) {
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text;
}

/**
 * @brief Renders one record field value for the page log.
 * @param value String, int, date, or list of links.
 * @return Text that fits inside a `{ key: … }` line.
 */
std::string formatFieldValue(const Value &value) {
    if (const auto *text = std::get_if<std::string>(&value)) {
        return "\"" + escapeQuoted(*text) + "\"";
    }
    if (const auto *number = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*number);
    }
    if (const auto *links = std::get_if<std::vector<Link>>(&value)) {
        std::ostringstream out;
        out << "[ ";
        for (std::size_t i = 0; i < links->size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            const Link &link = (*links)[i];
            out << "{ id: " << link.id << ", name: \"" << escapeQuoted(link.name) << "\" }";
        }
        out << " ]";
        return out.str();
    }
    const Date &date = std::get<Date>(value);
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << date.year << '-' << std::setw(2) << date.month
        << '-' << std::setw(2) << date.day;
    return out.str();
}

/**
 * @brief Renders the domain page the caller receives.
 * @param source `cache` or `backend`.
 * @param entity Queried entity name.
 * @param page Rows and continuation cursor.
 * @return One `page` body, with every field on each row.
 */
std::string formatPage(const char *source, const std::string &entity, const Page &page) {
    std::ostringstream out;
    out << "page source=" << source << " entity=" << entity << " rows=" << page.rows.size()
        << " next_cursor=";
    if (page.next_cursor) {
        out << '"' << escapeQuoted(*page.next_cursor) << '"';
    } else {
        out << '-';
    }
    for (const Record &row : page.rows) {
        out << "\n  { ";
        bool first = true;
        for (const auto &[key, value] : row.fields) {
            if (!first) {
                out << ", ";
            }
            first = false;
            out << key << ": " << formatFieldValue(value);
        }
        out << " }";
    }
    return out.str();
}

/**
 * @brief True when the process asked for the ShotGrid backend.
 * @return True when `MIKROS_PMS_BACKEND` is `shotgrid`.
 */
bool useShotGrid() { return backend().value == "shotgrid"; }

/**
 * @brief ShotGrid site values read from the environment.
 */
struct ShotGridEnv {
    std::string url;         ///< `SHOTGRID_URL`.
    std::string scriptName;  ///< `SHOTGRID_CLIENT_ID`.
    std::string scriptKey;   ///< `SHOTGRID_CLIENT_SECRET`.
    std::string mappingPath; ///< `SHOTGRID_MAPPING`.
};

/**
 * @brief Reads the four ShotGrid environment variables.
 * @return Site URL, script credentials, and mapping path.
 * @throw Error When one of them is missing or empty.
 */
ShotGridEnv readShotGridEnv() {
    const TextSetting url = shotGridUrl();
    const TextSetting script = shotGridClientId();
    const TextSetting key = shotGridClientSecret();
    const TextSetting mapping = shotGridMapping();
    if (!url.set || !script.set || !key.set || !mapping.set) {
        throw Error(
            "shotgrid: set SHOTGRID_URL, SHOTGRID_CLIENT_ID, "
            "SHOTGRID_CLIENT_SECRET, SHOTGRID_MAPPING");
    }
    return ShotGridEnv{url.value, script.value, key.value, mapping.value};
}

/**
 * @brief Logs the site URL, script name, redacted key, and mapping path.
 * @param env Values from `readShotGridEnv`.
 */
void logShotGridSite(const ShotGridEnv &env) {
    emitInfo(
        "shotgrid site url=\"" + escapeQuoted(env.url) + "\" script=\"" +
        escapeQuoted(env.scriptName) + "\" key=" + keyHint(env.scriptKey) + " mapping=\"" +
        escapeQuoted(env.mappingPath) + "\"");
}

/**
 * @brief Cache trace hooks, lifetime, and byte budget.
 * @param backendName `shotgrid` or `fake`.
 * @return Options passed to `CachingBackend`.
 */
CacheOptions buildCacheOptions(const std::string &backendName) {
    CacheOptions options;
    options.ttl = std::chrono::seconds(cacheTtlSeconds().value);
    options.max_bytes = static_cast<std::size_t>(cacheMaxBytes().value);
    options.backend_name = backendName;
    emitInfo(
        "cache ttl_seconds=" + std::to_string(options.ttl.count()) +
        " max_bytes=" + std::to_string(options.max_bytes));
    options.trace_ir = [](const Ir &ir) {
        emitInfo("request intermediate representation\n" + trimNewline(dump(ir)));
    };
    options.trace_cache = [](const char *verdict, const std::string &entity) {
        emitInfo(std::string("cache ") + verdict + " entity=" + entity);
    };
    options.trace_fallback = [](const std::string &name, const std::string &entity) {
        emitInfo("fallback backend=" + name + " entity=" + entity);
    };
    options.trace_page = [](const char *source, const std::string &entity, const Page &page) {
        emitInfo(formatPage(source, entity, page));
    };
    return options;
}

/**
 * @brief Schema path that must be non-empty before load.
 * @return Absolute path from `PMS_SCHEMA_PATH`.
 * @throw Error When `PMS_SCHEMA_PATH` is missing or empty.
 */
std::string requireSchemaPath() {
    const TextSetting path = schemaPath();
    if (path.value.empty()) {
        throw Error("set PMS_SCHEMA_PATH to the domain schema.yaml");
    }
    return path.value;
}

/**
 * @brief Shared backend + CachingBackend + Session for the process.
 *
 * Owns the concrete backend through `unique_ptr` so construction order stays
 * simple with or without FakeBackend.
 */
class SharedPms {
  public:
    /**
     * @brief Returns the process-wide pms session holder.
     * @return The single shared instance.
     */
    static SharedPms &getInstance() {
        static SharedPms shared;
        return shared;
    }

    /**
     * @brief Returns the session used by every find.
     * @return Session bound to the schema and the cache.
     */
    Session &getSession() { return session_; }

  private:
    SharedPms()
        : backend_(buildBackend()),
          cache_(*backend_, buildCacheOptions(useShotGrid() ? "shotgrid" : "fake")),
          session_(Schema::load(requireSchemaPath()), cache_) {}

    /**
     * @brief Builds the selected concrete backend.
     * @return Owned backend ready for the cache.
     * @throw Error When fake mode is selected without FakeBackend, or ShotGrid
     *     env is incomplete.
     */
    static std::unique_ptr<Backend> buildBackend() {
        if (useShotGrid()) {
            const ShotGridEnv env = readShotGridEnv();
            logShotGridSite(env);
            ShotGridSite site;
            site.base_url = env.url;
            site.auth = ShotGridScriptAuth{env.scriptName, env.scriptKey};
            auto sg = std::make_unique<ShotGridBackend>(std::move(site), env.mappingPath);
            sg->setSearchLog(
                [](const std::string &payload) { emitInfo("shotgrid payload " + payload); });
            sg->setAuthLog([](const std::string &line) { emitInfo("shotgrid " + line); });
            return sg;
        }
#if defined(PMS_HAS_FAKE) && PMS_HAS_FAKE
        return std::unique_ptr<Backend>(new FakeBackend(/*simulate_latency=*/false));
#else
        throw Error("MIKROS_PMS_BACKEND=fake but FakeBackend was not built (copy pms/dev/)");
#endif
    }

    std::unique_ptr<Backend> backend_; ///< Fake or ShotGrid, selected at launch.
    CachingBackend cache_;             ///< Serializes finds and traces them.
    Session session_;                  ///< Schema plus the cache.
};

} // namespace

void setSharedInfoLog(std::function<void(std::string_view)> log) { infoSink() = std::move(log); }

Session &sharedSession() { return SharedPms::getInstance().getSession(); }

} // namespace pms
