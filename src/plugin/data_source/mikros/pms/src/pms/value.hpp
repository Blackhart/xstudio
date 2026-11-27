// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/value.hpp
 * @brief Values and the record returned by find().
 */
#pragma once

#include "pms/error.hpp"

#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace pms {

/**
 * @brief Calendar day with no time and no time zone.
 *
 * A date is valid when month and day exist in that year, including leap days.
 */
struct Date {
    int year = 0;  ///< Four-digit year as parsed from the text.
    int month = 0; ///< Month in the range 1-12.
    int day = 0;   ///< Day in the range valid for `month`.

    /**
     * @brief Compares two dates by year, month, and day.
     * @param other Date to compare with.
     * @return True when the three fields are equal.
     */
    bool operator==(const Date &other) const {
        return year == other.year && month == other.month && day == other.day;
    }
};

/**
 * @brief Proxy tag for a datetime field such as created_at.
 *
 * The record still stores the ShotGrid timestamp as a string. Only the
 * generated Field proxy uses this type, so date operators do not apply to
 * every string field.
 */
struct DateTime {};

/**
 * @brief Proxy tag for an entity or multi_entity field.
 *
 * `author` is one linked record. `sites` and `departments` are a list of
 * linked records. Both use this tag, so they share `nameContains`,
 * `nameNotContains`, `nameIs`, `typeIs`, and `typeIsNot`. The record stores one `Link` for an entity
 * and a list of `Link` for a multi_entity. A screen that wants a joined label
 * builds it in its DAL. Only the generated Field proxy uses this type, so
 * those operators do not apply to plain text fields.
 */
struct EntityRef {};

/**
 * @brief Inclusive pair passed to between on an int field.
 */
struct IntBounds {
    std::int64_t low = 0;  ///< Inclusive lower bound.
    std::int64_t high = 0; ///< Inclusive upper bound.

    /**
     * @brief Compares two int ranges.
     * @param other Range to compare with.
     * @return True when both bounds are equal.
     */
    bool operator==(const IntBounds &other) const {
        return low == other.low && high == other.high;
    }
};

/**
 * @brief Inclusive pair passed to between on a float field.
 */
struct FloatBounds {
    double low = 0;  ///< Inclusive lower bound.
    double high = 0; ///< Inclusive upper bound.

    /**
     * @brief Compares two float ranges.
     * @param other Range to compare with.
     * @return True when both bounds are equal.
     */
    bool operator==(const FloatBounds &other) const {
        return low == other.low && high == other.high;
    }
};

/**
 * @brief Inclusive pair of calendar days passed to between on a datetime.
 */
struct DateBounds {
    Date low;  ///< Inclusive first day.
    Date high; ///< Inclusive last day.

    /**
     * @brief Compares two date ranges.
     * @param other Range to compare with.
     * @return True when both bounds are equal.
     */
    bool operator==(const DateBounds &other) const {
        return low == other.low && high == other.high;
    }
};

/**
 * @brief Relative span passed to in_last.
 *
 * `count` is a positive number of `unit` steps. ShotGrid receives
 * `[count, unit]`.
 */
struct Period {
    std::int64_t count = 0; ///< Positive count of units.
    std::string unit;       ///< `HOUR`, `DAY`, `WEEK`, `MONTH`, or `YEAR`.

    /**
     * @brief Compares two periods by count and unit.
     * @param other Period to compare with.
     * @return True when both fields are equal.
     */
    bool operator==(const Period &other) const {
        return count == other.count && unit == other.unit;
    }
};

/**
 * @brief One linked entity, alone or inside a multi-entity list.
 *
 * `id` is the linked entity id. `name` is the label ShotGrid puts on the link.
 * `type` is the ShotGrid entity type, such as `Shot` or `Playlist`. `image`
 * is an optional thumbnail URL. Equality ignores `image`: two links are the
 * same row when id, name, and type match.
 */
struct Link {
    std::int64_t id = 0; ///< Linked entity id.
    std::string name;    ///< Linked entity label.
    std::string type;    ///< ShotGrid entity type. Empty when the payload has none.
    std::string image;   ///< Thumbnail URL. Empty when the link has none.

    /**
     * @brief Compares two links by id, name, and type.
     * @param other Link to compare with.
     * @return True when id, name, and type are equal. `image` is display-only.
     */
    bool operator==(const Link &other) const {
        return id == other.id && name == other.name && type == other.type;
    }
};

/**
 * @brief Value stored in a filter leaf or in a record field.
 *
 * `text` maps to `std::string`, `number` to `std::int64_t`, `float` to
 * `double`, and `checkbox` to `bool`. An entity field stores one `Link`. A
 * multi_entity field stores a list of `Link`. An `in` leaf stores a list. An
 * `in_last` leaf stores a Period. A `between` leaf stores bounds.
 * `DateTime` and `EntityRef` are field proxies, not values. `created_at`
 * stays a string on the record.
 */
using Value = std::variant<
    std::string,
    std::int64_t,
    double,
    bool,
    Date,
    Link,
    std::vector<Link>,
    std::vector<std::string>,
    std::vector<std::int64_t>,
    std::vector<double>,
    Period,
    IntBounds,
    FloatBounds,
    DateBounds>;

/**
 * @brief One row returned by find(), keyed by the domain YAML names.
 *
 * A missing key means the backend did not send it. The library does not insert
 * a null in its place. `id` and `name` are required and checked after find().
 */
struct Record {
    std::map<std::string, Value> fields; ///< Domain keys and their values.

    /**
     * @brief Returns the value stored at `key`.
     * @param key Domain field name, such as `"name"` or `"code"`.
     * @return The stored scalar.
     * @throw std::out_of_range When `key` is absent.
     */
    const Value &at(const std::string &key) const { return fields.at(key); }

    /**
     * @brief Looks up `key` without throwing.
     * @param key Domain field name.
     * @return Iterator to the entry, or `end()` when the key is absent.
     */
    std::map<std::string, Value>::const_iterator find(const std::string &key) const {
        return fields.find(key);
    }
};

/**
 * @brief Rows returned by find().
 *
 * An empty vector means the filter matched nothing. That is not an error.
 */
using Records = std::vector<Record>;

/**
 * @brief Reports whether `year` is a Gregorian leap year.
 * @param year Year to test.
 * @return True for years divisible by 4, except centuries not divisible by 400.
 */
inline bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * @brief Returns the number of days in a month.
 * @param year Year, used for February.
 * @param month Month in the range 1-12.
 * @return The day count, or 0 when `month` is outside 1-12.
 */
inline int daysInMonth(int year, int month) {
    static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    if (month < 1 || month > 12) {
        return 0;
    }
    return days[month];
}

/**
 * @brief Parses an ISO calendar date.
 * @param text Text in the exact form `YYYY-MM-DD`.
 * @return The parsed day.
 * @throw Error When the text is not that form or the day does not exist.
 */
inline Date parseDate(const std::string &text) {
    const auto bad = [&text]() { throw Error("invalid date \"" + text + "\""); };
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        bad();
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
            bad();
        }
    }
    const int year = std::stoi(text.substr(0, 4));
    const int month = std::stoi(text.substr(5, 2));
    const int day = std::stoi(text.substr(8, 2));
    if (day < 1 || day > daysInMonth(year, month)) {
        bad();
    }
    return Date{year, month, day};
}

} // namespace pms
