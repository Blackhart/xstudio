// SPDX-License-Identifier: Apache-2.0
/**
 * @file fake/backend.cpp
 * @brief Fixed catalogs and In/Where matching, including weak playlist memberships.
 */

#include "pms/fake/backend.hpp"

#include "pms/error.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace pms {
namespace {

Records makeProjects() {
    const char *statuses[] = {"ip", "wtg", "fin", "hld"};
    Records rows;
    rows.reserve(FakeBackend::projectCount);
    for (int index = 1; index <= static_cast<int>(FakeBackend::projectCount); ++index) {
        const std::string number = (index < 10 ? "0" : "") + std::to_string(index);
        Record row;
        row.fields["id"] = std::int64_t{index};
        row.fields["name"] = "paw" + number;
        row.fields["code"] = "PAW" + number;
        row.fields["status"] = std::string(statuses[(index - 1) % 4]);
        row.fields["start_date"] = Date{2026, 1, index};
        row.fields["delivery_date"] = Date{2026, 6, index};
        rows.push_back(std::move(row));
    }
    return rows;
}

/**
 * @brief Builds two sequences per project (`pawNN_sq010`, `pawNN_sq020`).
 *
 * The private `project` int links a sequence to its project id.
 */
Records makeSequences() {
    Records rows;
    rows.reserve(FakeBackend::projectCount * FakeBackend::sequencesPerProject);
    std::int64_t sequenceId = 1;
    for (int projectId = 1; projectId <= static_cast<int>(FakeBackend::projectCount);
         ++projectId) {
        const std::string projectNumber =
            (projectId < 10 ? "0" : "") + std::to_string(projectId);
        for (std::size_t index = 1; index <= FakeBackend::sequencesPerProject; ++index) {
            const int shotNumber = static_cast<int>(index) * 10;
            const std::string sequenceNumber =
                (shotNumber < 100 ? "0" : "") + std::to_string(shotNumber);
            Record row;
            row.fields["id"] = sequenceId++;
            row.fields["name"] = "paw" + projectNumber + "_sq" + sequenceNumber;
            row.fields["project"] = std::int64_t{projectId};
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

/**
 * @brief Builds two shots per sequence (`…_sh010`, `…_sh020`).
 *
 * Private `sequence` and `project` ints support In(sequence) and In(project).
 */
Records makeShots(const Records &sequences) {
    Records rows;
    rows.reserve(sequences.size() * FakeBackend::shotsPerSequence);
    std::int64_t shotId = 1;
    for (const Record &sequence : sequences) {
        const std::string sequenceName = std::get<std::string>(sequence.fields.at("name"));
        for (std::size_t index = 1; index <= FakeBackend::shotsPerSequence; ++index) {
            const int shotNumber = static_cast<int>(index) * 10;
            const std::string shotLabel =
                (shotNumber < 100 ? "0" : "") + std::to_string(shotNumber);
            Record row;
            row.fields["id"] = shotId++;
            row.fields["name"] = sequenceName + "_sh" + shotLabel;
            row.fields["sequence"] = sequence.fields.at("id");
            row.fields["project"] = sequence.fields.at("project");
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

/**
 * @brief Builds two assets per project (`pawNN_ast01`, `pawNN_ast02`).
 *
 * The private `project` int links an asset to its project id.
 */
Records makeAssets() {
    Records rows;
    rows.reserve(FakeBackend::projectCount * FakeBackend::assetsPerProject);
    std::int64_t assetId = 1;
    for (int projectId = 1; projectId <= static_cast<int>(FakeBackend::projectCount);
         ++projectId) {
        const std::string projectNumber =
            (projectId < 10 ? "0" : "") + std::to_string(projectId);
        for (std::size_t index = 1; index <= FakeBackend::assetsPerProject; ++index) {
            const std::string assetNumber =
                (index < 10 ? "0" : "") + std::to_string(index);
            Record row;
            row.fields["id"] = assetId++;
            row.fields["name"] = "paw" + projectNumber + "_ast" + assetNumber;
            row.fields["project"] = std::int64_t{projectId};
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

/**
 * @brief Builds two playlists per project (`pawNN_pl01`, `pawNN_pl02`).
 *
 * The private `project` int links a playlist to its project id.
 */
Records makePlaylists() {
    Records rows;
    rows.reserve(FakeBackend::projectCount * FakeBackend::playlistsPerProject);
    std::int64_t playlistId = 1;
    for (int projectId = 1; projectId <= static_cast<int>(FakeBackend::projectCount);
         ++projectId) {
        const std::string projectNumber =
            (projectId < 10 ? "0" : "") + std::to_string(projectId);
        for (std::size_t index = 1; index <= FakeBackend::playlistsPerProject; ++index) {
            const std::string playlistNumber =
                (index < 10 ? "0" : "") + std::to_string(index);
            Record row;
            row.fields["id"] = playlistId++;
            row.fields["name"] = "paw" + projectNumber + "_pl" + playlistNumber;
            row.fields["project"] = std::int64_t{projectId};
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

/**
 * @brief Builds weak playlist memberships for existing versions.
 *
 * `_v001` versions join `pl01`, `_v002` join `pl02`. The first sequence `_v001`
 * of each project also joins `pl02`, so one version can sit in two playlists.
 *
 * @param versions Catalog of strongly parented versions.
 * @param playlists Playlist catalog.
 * @return version id → playlist ids.
 */
std::map<std::int64_t, std::vector<std::int64_t>> linkVersionsToPlaylists(
    const Records &versions, const Records &playlists) {
    std::map<std::int64_t, std::vector<std::int64_t>> memberships;
    std::map<std::int64_t, std::vector<std::int64_t>> playlistsByProject;
    for (const Record &playlist : playlists) {
        const auto projectId = std::get<std::int64_t>(playlist.fields.at("project"));
        playlistsByProject[projectId].push_back(
            std::get<std::int64_t>(playlist.fields.at("id")));
    }

    for (const Record &version : versions) {
        const auto versionId = std::get<std::int64_t>(version.fields.at("id"));
        const auto projectId = std::get<std::int64_t>(version.fields.at("project"));
        const auto &projectPlaylists = playlistsByProject[projectId];
        if (projectPlaylists.empty()) {
            continue;
        }
        const std::string name = std::get<std::string>(version.fields.at("name"));
        const bool isV001 =
            name.size() >= 5 && name.compare(name.size() - 5, 5, "_v001") == 0;
        const bool isV002 =
            name.size() >= 5 && name.compare(name.size() - 5, 5, "_v002") == 0;
        if (isV001) {
            memberships[versionId].push_back(projectPlaylists.front());
        }
        if (isV002 && projectPlaylists.size() > 1) {
            memberships[versionId].push_back(projectPlaylists[1]);
        }
    }

    // One multi-playlist sample per project: first sequence `_v001` also in pl02.
    for (const auto &[projectId, projectPlaylists] : playlistsByProject) {
        if (projectPlaylists.size() < 2) {
            continue;
        }
        for (const Record &version : versions) {
            if (std::get<std::int64_t>(version.fields.at("project")) != projectId) {
                continue;
            }
            if (version.fields.count("sequence") == 0 || version.fields.count("shot") != 0) {
                continue;
            }
            const std::string name = std::get<std::string>(version.fields.at("name"));
            if (name.size() < 5 || name.compare(name.size() - 5, 5, "_v001") != 0) {
                continue;
            }
            const auto versionId = std::get<std::int64_t>(version.fields.at("id"));
            auto &links = memberships[versionId];
            if (std::find(links.begin(), links.end(), projectPlaylists[1]) == links.end()) {
                links.push_back(projectPlaylists[1]);
            }
            break;
        }
    }
    return memberships;
}

/**
 * @brief Builds sequence-linked, shot-linked, and asset-linked versions.
 *
 * Sequence versions omit `shot` and `asset`. Shot versions set `shot`,
 * `sequence`, and `project`. Asset versions set `asset` and `project`.
 * Playlist membership is stored separately (weak many-to-many).
 */
Records makeVersions(
    const Records &sequences, const Records &shots, const Records &assets) {
    Records rows;
    rows.reserve(
        sequences.size() * FakeBackend::versionsPerSequence +
        shots.size() * FakeBackend::versionsPerShot +
        assets.size() * FakeBackend::versionsPerAsset);
    std::int64_t versionId = 1;

    for (const Record &sequence : sequences) {
        const std::string sequenceName = std::get<std::string>(sequence.fields.at("name"));
        for (std::size_t index = 1; index <= FakeBackend::versionsPerSequence; ++index) {
            const std::string versionNumber =
                (index < 10 ? "00" : (index < 100 ? "0" : "")) + std::to_string(index);
            Record row;
            row.fields["id"] = versionId++;
            row.fields["name"] = sequenceName + "_v" + versionNumber;
            row.fields["sequence"] = sequence.fields.at("id");
            row.fields["project"] = sequence.fields.at("project");
            rows.push_back(std::move(row));
        }
    }

    for (const Record &shot : shots) {
        const std::string shotName = std::get<std::string>(shot.fields.at("name"));
        for (std::size_t index = 1; index <= FakeBackend::versionsPerShot; ++index) {
            const std::string versionNumber =
                (index < 10 ? "00" : (index < 100 ? "0" : "")) + std::to_string(index);
            Record row;
            row.fields["id"] = versionId++;
            row.fields["name"] = shotName + "_v" + versionNumber;
            row.fields["shot"] = shot.fields.at("id");
            row.fields["sequence"] = shot.fields.at("sequence");
            row.fields["project"] = shot.fields.at("project");
            rows.push_back(std::move(row));
        }
    }

    for (const Record &asset : assets) {
        const std::string assetName = std::get<std::string>(asset.fields.at("name"));
        for (std::size_t index = 1; index <= FakeBackend::versionsPerAsset; ++index) {
            const std::string versionNumber =
                (index < 10 ? "00" : (index < 100 ? "0" : "")) + std::to_string(index);
            Record row;
            row.fields["id"] = versionId++;
            row.fields["name"] = assetName + "_v" + versionNumber;
            row.fields["asset"] = asset.fields.at("id");
            row.fields["project"] = asset.fields.at("project");
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

bool matchesWhere(const Record &row, const ExprNode &node) {
    if (node.kind == ExprKind::Group) {
        if (node.op == ExprOp::Not) {
            if (node.args.size() != 1 || !node.args[0]) {
                throw Error("fake backend: not expects one child");
            }
            return !matchesWhere(row, *node.args[0]);
        }
        if (node.op != ExprOp::And && node.op != ExprOp::Or) {
            throw Error("fake backend: invalid group");
        }
        if (node.args.size() < 2) {
            throw Error("fake backend: group expects at least two children");
        }
        for (const auto &child : node.args) {
            if (!child) {
                throw Error("fake backend: empty child");
            }
            const bool childMatches = matchesWhere(row, *child);
            if (node.op == ExprOp::And && !childMatches) {
                return false;
            }
            if (node.op == ExprOp::Or && childMatches) {
                return true;
            }
        }
        return node.op == ExprOp::And;
    }

    if (node.kind != ExprKind::Field) {
        throw Error("fake backend: where expects a field leaf");
    }
    if (!node.value) {
        throw Error("fake backend: field \"" + node.key + "\" has no value");
    }
    const auto found = row.fields.find(node.key);
    const bool equal = found != row.fields.end() && found->second == *node.value;
    if (node.op == ExprOp::Eq) {
        return equal;
    }
    if (node.op == ExprOp::Ne) {
        return !equal;
    }
    throw Error("fake backend: operator not allowed on \"" + node.key + "\"");
}

std::optional<std::int64_t> idByName(const Records &catalog, const std::string &name) {
    for (const Record &row : catalog) {
        const auto found = row.fields.find("name");
        if (found != row.fields.end() && found->second == Value{name}) {
            return std::get<std::int64_t>(row.fields.at("id"));
        }
    }
    return std::nullopt;
}

/**
 * @brief Resolves a scope leaf to a parent id from the parent catalog.
 */
std::optional<std::int64_t> parentId(
    const Records &parents, const ExprNode &node, const char *parentKey) {
    if (node.key != parentKey) {
        throw Error("fake backend: parent \"" + node.key + "\" is not available");
    }
    if (!node.value) {
        throw Error(std::string("fake backend: parent \"") + parentKey + "\" has no value");
    }
    if (const auto *id = std::get_if<std::int64_t>(&*node.value)) {
        return *id;
    }
    if (const auto *name = std::get_if<std::string>(&*node.value)) {
        return idByName(parents, *name);
    }
    throw Error(std::string("fake backend: parent \"") + parentKey + "\" expects an id or a name");
}

bool matchesIn(
    const Record &row,
    const Records &parents,
    const ExprNode &node,
    const char *parentKey,
    const char *linkField) {
    if (node.kind == ExprKind::Group) {
        if (node.op == ExprOp::Not) {
            if (node.args.size() != 1 || !node.args[0]) {
                throw Error("fake backend: not expects one child");
            }
            return !matchesIn(row, parents, *node.args[0], parentKey, linkField);
        }
        if (node.op != ExprOp::And && node.op != ExprOp::Or) {
            throw Error("fake backend: invalid group");
        }
        if (node.args.size() < 2) {
            throw Error("fake backend: group expects at least two children");
        }
        for (const auto &child : node.args) {
            if (!child) {
                throw Error("fake backend: empty child");
            }
            const bool childMatches = matchesIn(row, parents, *child, parentKey, linkField);
            if (node.op == ExprOp::And && !childMatches) {
                return false;
            }
            if (node.op == ExprOp::Or && childMatches) {
                return true;
            }
        }
        return node.op == ExprOp::And;
    }

    if (node.kind != ExprKind::Scope) {
        throw Error("fake backend: in expects a scope leaf");
    }
    const auto expected = parentId(parents, node, parentKey);
    const auto linked = row.fields.find(linkField);
    const bool equal =
        expected && linked != row.fields.end() && linked->second == Value{*expected};
    if (node.op == ExprOp::Eq) {
        return equal;
    }
    if (node.op == ExprOp::Ne) {
        return !equal;
    }
    throw Error("fake backend: operator not allowed on parent \"" + node.key + "\"");
}

/**
 * @brief Matches an In tree whose leaves may name different parents.
 *
 * @param row Catalog row with private link fields.
 * @param node In expression root.
 * @param matchLeaf Resolves one scope leaf against the correct parent catalog.
 */
template <typename MatchLeaf>
bool matchesScopedIn(const Record &row, const ExprNode &node, MatchLeaf &&matchLeaf) {
    if (node.kind == ExprKind::Group) {
        if (node.op == ExprOp::Not) {
            if (node.args.size() != 1 || !node.args[0]) {
                throw Error("fake backend: not expects one child");
            }
            return !matchesScopedIn(row, *node.args[0], matchLeaf);
        }
        if (node.op != ExprOp::And && node.op != ExprOp::Or) {
            throw Error("fake backend: invalid group");
        }
        if (node.args.size() < 2) {
            throw Error("fake backend: group expects at least two children");
        }
        for (const auto &child : node.args) {
            if (!child) {
                throw Error("fake backend: empty child");
            }
            const bool childMatches = matchesScopedIn(row, *child, matchLeaf);
            if (node.op == ExprOp::And && !childMatches) {
                return false;
            }
            if (node.op == ExprOp::Or && childMatches) {
                return true;
            }
        }
        return node.op == ExprOp::And;
    }
    if (node.kind != ExprKind::Scope) {
        throw Error("fake backend: in expects a scope leaf");
    }
    return matchLeaf(row, node);
}

Record publicNamed(const Record &row) {
    Record out;
    out.fields["id"] = row.fields.at("id");
    out.fields["name"] = row.fields.at("name");
    return out;
}

Records selectWhere(const Records &catalog, const std::shared_ptr<const ExprNode> &where) {
    if (!where) {
        return catalog;
    }
    Records rows;
    for (const Record &row : catalog) {
        if (matchesWhere(row, *where)) {
            rows.push_back(row);
        }
    }
    return rows;
}

} // namespace

FakeBackend::FakeBackend(bool simulate_latency)
    : simulate_latency_(simulate_latency),
      projects_(makeProjects()),
      sequences_(makeSequences()),
      shots_(makeShots(sequences_)),
      assets_(makeAssets()),
      playlists_(makePlaylists()),
      versions_(makeVersions(sequences_, shots_, assets_)),
      version_playlists_(linkVersionsToPlaylists(versions_, playlists_)) {}

void FakeBackend::maybeSleep() const {
    if (!simulate_latency_) {
        return;
    }
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> delay_ms(200, 2500);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms(rng)));
}

Page FakeBackend::find(const Ir &ir) {
    Records matched = matchAll(ir);
    Page page;
    if (!ir.limit) {
        page.rows = std::move(matched);
        maybeSleep();
        return page;
    }
    std::int64_t afterId = 0;
    if (ir.cursor) {
        try {
            afterId = std::stoll(*ir.cursor);
        } catch (const std::exception &) {
            throw Error("fake backend: cursor must be an integer id");
        }
    }
    for (const Record &row : matched) {
        const auto id = std::get<std::int64_t>(row.at("id"));
        if (id <= afterId) {
            continue;
        }
        page.rows.push_back(row);
        if (page.rows.size() == *ir.limit) {
            break;
        }
    }
    if (!page.rows.empty()) {
        const auto lastId = std::get<std::int64_t>(page.rows.back().at("id"));
        const bool more = std::any_of(
            matched.begin(),
            matched.end(),
            [lastId](const Record &row) {
                return std::get<std::int64_t>(row.at("id")) > lastId;
            });
        if (more) {
            page.next_cursor = std::to_string(lastId);
        }
    }
    maybeSleep();
    return page;
}

Records FakeBackend::matchAll(const Ir &ir) const {
    if (!ir.op || *ir.op != Op::Find) {
        throw Error("fake backend: missing operation");
    }

    Records rows;
    if (ir.entity == "project") {
        if (ir.in) {
            throw Error("fake backend: project has no parents");
        }
        rows = selectWhere(projects_, ir.where);
    } else if (ir.entity == "sequence") {
        for (const Record &sequence : sequences_) {
            if (ir.in && !matchesIn(sequence, projects_, *ir.in, "project", "project")) {
                continue;
            }
            if (ir.where && !matchesWhere(sequence, *ir.where)) {
                continue;
            }
            rows.push_back(publicNamed(sequence));
        }
    } else if (ir.entity == "shot") {
        for (const Record &shot : shots_) {
            if (ir.in) {
                const auto matchShotLeaf = [&](const Record &row, const ExprNode &node) {
                    if (node.key == "sequence") {
                        return matchesIn(row, sequences_, node, "sequence", "sequence");
                    }
                    if (node.key == "project") {
                        return matchesIn(row, projects_, node, "project", "project");
                    }
                    throw Error("fake backend: parent \"" + node.key + "\" is not available");
                };
                if (!matchesScopedIn(shot, *ir.in, matchShotLeaf)) {
                    continue;
                }
            }
            if (ir.where && !matchesWhere(shot, *ir.where)) {
                continue;
            }
            rows.push_back(publicNamed(shot));
        }
    } else if (ir.entity == "asset") {
        for (const Record &asset : assets_) {
            if (ir.in && !matchesIn(asset, projects_, *ir.in, "project", "project")) {
                continue;
            }
            if (ir.where && !matchesWhere(asset, *ir.where)) {
                continue;
            }
            rows.push_back(publicNamed(asset));
        }
    } else if (ir.entity == "playlist") {
        for (const Record &playlist : playlists_) {
            if (ir.in && !matchesIn(playlist, projects_, *ir.in, "project", "project")) {
                continue;
            }
            if (ir.where && !matchesWhere(playlist, *ir.where)) {
                continue;
            }
            rows.push_back(publicNamed(playlist));
        }
    } else if (ir.entity == "version") {
        for (const Record &version : versions_) {
            if (ir.in) {
                const auto matchVersionLeaf = [&](const Record &row, const ExprNode &node) {
                    if (node.key == "shot") {
                        return matchesIn(row, shots_, node, "shot", "shot");
                    }
                    if (node.key == "sequence") {
                        return matchesIn(row, sequences_, node, "sequence", "sequence");
                    }
                    if (node.key == "asset") {
                        return matchesIn(row, assets_, node, "asset", "asset");
                    }
                    if (node.key == "playlist") {
                        const auto expected = parentId(playlists_, node, "playlist");
                        const auto versionId = std::get<std::int64_t>(row.fields.at("id"));
                        const auto found = version_playlists_.find(versionId);
                        const bool member =
                            expected && found != version_playlists_.end() &&
                            std::find(found->second.begin(), found->second.end(), *expected) !=
                                found->second.end();
                        if (node.op == ExprOp::Eq) {
                            return member;
                        }
                        if (node.op == ExprOp::Ne) {
                            return !member;
                        }
                        throw Error(
                            "fake backend: operator not allowed on parent \"" + node.key + "\"");
                    }
                    if (node.key == "project") {
                        return matchesIn(row, projects_, node, "project", "project");
                    }
                    throw Error("fake backend: parent \"" + node.key + "\" is not available");
                };
                if (!matchesScopedIn(version, *ir.in, matchVersionLeaf)) {
                    continue;
                }
            }
            if (ir.where && !matchesWhere(version, *ir.where)) {
                continue;
            }
            rows.push_back(publicNamed(version));
        }
    } else {
        throw Error("fake backend: entity \"" + ir.entity + "\" is not available");
    }

    return rows;
}

} // namespace pms
