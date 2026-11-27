// SPDX-License-Identifier: Apache-2.0
/**
 * @file pms/fake/backend.hpp
 * @brief In-memory backend for project, sequence, shot, asset, playlist, version, and note catalogs.
 */
#pragma once

#include "pms/backend.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace pms {

/**
 * @brief Returns fixed catalogs for the domain entities used by the Shot Browser.
 *
 * Sequence, asset, and playlist rows use In(project). Shot rows use In(sequence)
 * or In(project). Version rows keep a strong parent (shot, sequence, or asset)
 * and may also belong to one or more playlists. In(playlist) uses that weak
 * membership. Shot version counts are sized so one project spans about four
 * pages at the default page size. Returned sequence, shot,
 * asset records expose `id` and `name`; playlists and versions also expose
 * their card fields. When
 * `ir.limit` is set, find() keysets on `id`. count() uses the same filters and
 * returns the match total. When `simulate_latency` is true, find() and count()
 * sleep 1.5–2.5 s before returning (long enough to see Loading… in the UI).
 */
class FakeBackend : public Backend {
  public:
    static constexpr std::size_t projectCount = 20; ///< Size of the project catalog.
    static constexpr std::size_t sequencesPerProject =
        2; ///< Sequences generated for each project.
    static constexpr std::size_t shotsPerSequence =
        2; ///< Shots generated for each sequence.
    static constexpr std::size_t assetsPerProject =
        2; ///< Assets generated for each project.
    static constexpr std::size_t playlistsPerProject =
        200; ///< Playlists per project (~4 pages at the default page size of 50).
    static constexpr std::size_t versionsPerSequence =
        2; ///< Versions linked directly to each sequence.
    static constexpr std::size_t versionsPerShot =
        50; ///< Versions per shot (~4 project pages at the default page size of 50).
    static constexpr std::size_t versionsPerAsset =
        2; ///< Versions linked to each asset.

    /**
     * @brief Fills the catalogs and weak playlist memberships.
     * @param simulate_latency When true, find() adds a random exit delay.
     */
    explicit FakeBackend(bool simulate_latency = false);

    /**
     * @brief Enables or disables the random exit delay on later calls.
     * @param simulate_latency New latency flag.
     */
    void setSimulateLatency(bool simulate_latency) { simulate_latency_ = simulate_latency; }

    /**
     * @brief Returns matching rows, optionally one keyset page.
     * @param ir Find query. Pagination uses `limit` / `cursor` when set.
     * @return Page of rows. `next_cursor` is set only when a limit leaves more.
     * @throw Error When the verb is not find, the entity is unknown, or a tree
     *     is malformed.
     */
    Page find(const Ir &ir) override;

    /**
     * @brief Returns how many rows match the filters.
     * @param ir Count query. Same filters as find(); no pagination.
     * @return Match count.
     * @throw Error When the verb is not count, the entity is unknown, or a tree
     *     is malformed.
     */
    std::size_t count(const Ir &ir) override;

    /**
     * @brief Accepts a validated update and keeps the in-memory row unchanged.
     * @param ir Update query. Ignored by the example catalog.
     */
    void update(const Ir &ir) override;

  private:
    /**
     * @brief Matches the query against the catalogs without sleeping.
     * @param ir Find or count query. Pagination fields are ignored.
     * @return All matching public rows in catalog order.
     */
    Records matchAll(const Ir &ir) const;

    /** @brief Optionally sleeps when simulate_latency_ is true. */
    void maybeSleep() const;

    bool simulate_latency_ = false; ///< Random sleep on find or count exit when true.
    Records projects_;  ///< The twenty projects, in id order.
    Records sequences_; ///< Sequences linked to projects.
    Records shots_;     ///< Shots linked to sequences.
    Records assets_;    ///< Assets linked to projects.
    Records playlists_; ///< Playlists linked to projects.
    Records versions_;  ///< Versions linked to a shot, sequence, or asset.
    /// Weak many-to-many: version id → playlist ids (a version may appear in several).
    std::map<std::int64_t, std::vector<std::int64_t>> version_playlists_;
    Records notes_; ///< Notes linked to a version. Two when that version's open count is 2.
};

} // namespace pms
