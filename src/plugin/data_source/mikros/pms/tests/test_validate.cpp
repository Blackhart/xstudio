// SPDX-License-Identifier: Apache-2.0

#include "pms/pms.hpp"

#include <doctest/doctest.h>

using namespace pms;
using namespace pms::project;

namespace {

Schema schema() { return Schema::load(PMS_SCHEMA_PATH); }

Ir fieldIr(const std::string &key, ExprOp op, Value value) {
    auto leaf = std::make_shared<ExprNode>();
    leaf->op = op;
    leaf->kind = ExprKind::Field;
    leaf->key = key;
    leaf->value = std::move(value);
    Ir ir;
    ir.op = Op::Find;
    ir.entity = "project";
    ir.where = leaf;
    return ir;
}

} // namespace

TEST_CASE("validate accepts a project filter and an empty where") {
    const Schema loaded = schema();
    Ir listed;
    listed.op = Op::Find;
    listed.entity = "project";
    CHECK_NOTHROW(validate(listed, loaded));

    const Ir byId = fieldIr("id", ExprOp::Eq, std::int64_t{42});
    CHECK_NOTHROW(validate(byId, loaded));

    const Ir byNamePart = fieldIr("name", ExprOp::Contains, std::string("paw"));
    CHECK_NOTHROW(validate(byNamePart, loaded));
}

TEST_CASE("validate rejects contains on an int field") {
    const Schema loaded = schema();
    CHECK_THROWS_WITH_AS(
        validate(fieldIr("id", ExprOp::Contains, std::string("42")), loaded),
        "where: operator not allowed on \"id\"",
        Error);
}

TEST_CASE("validate rejects a broken tree") {
    const Schema loaded = schema();

    Ir bare;
    bare.entity = "project";
    bare.where = fieldIr("name", ExprOp::Eq, std::string("paw")).where;
    CHECK_THROWS_WITH_AS(validate(bare, loaded), "missing operation", Error);

    Ir unknownEntity;
    unknownEntity.op = Op::Find;
    unknownEntity.entity = "episode";
    CHECK_THROWS_WITH_AS(validate(unknownEntity, loaded), "unknown entity \"episode\"", Error);

    CHECK_THROWS_WITH_AS(
        validate(fieldIr("nope", ExprOp::Eq, std::string("x")), loaded),
        "where: unknown field \"nope\"",
        Error);
    CHECK_THROWS_WITH_AS(
        validate(fieldIr("id", ExprOp::Eq, std::string("42")), loaded),
        "where: type mismatch on \"id\"",
        Error);

    auto group = std::make_shared<ExprNode>();
    group->op = ExprOp::Not;
    group->kind = ExprKind::Group;
    group->args = {fieldIr("code", ExprOp::Eq, std::string("PAW01")).where,
                   fieldIr("name", ExprOp::Eq, std::string("paw")).where};
    Ir badNot;
    badNot.op = Op::Find;
    badNot.entity = "project";
    badNot.where = group;
    CHECK_THROWS_WITH_AS(validate(badNot, loaded), "where: not expects one child", Error);
}

/**
 * @brief Builds an In() field leaf for parent.id == value.
 */
std::shared_ptr<ExprNode> parentIdLeaf(const char *parent, std::int64_t id) {
    auto leaf = std::make_shared<ExprNode>();
    leaf->op = ExprOp::Eq;
    leaf->kind = ExprKind::Field;
    leaf->parent = parent;
    leaf->key = "id";
    leaf->value = id;
    return leaf;
}

TEST_CASE("validate accepts sequence, shot, asset, and inherited version in clauses") {
    const Schema loaded = schema();

    auto projectLeaf = parentIdLeaf("project", 1);
    Ir sequenceIr;
    sequenceIr.op = Op::Find;
    sequenceIr.entity = "sequence";
    sequenceIr.in = projectLeaf;
    CHECK_NOTHROW(validate(sequenceIr, loaded));

    Ir assetIr;
    assetIr.op = Op::Find;
    assetIr.entity = "asset";
    assetIr.in = projectLeaf;
    CHECK_NOTHROW(validate(assetIr, loaded));

    auto sequenceLeaf = parentIdLeaf("sequence", 1);
    Ir shotIr;
    shotIr.op = Op::Find;
    shotIr.entity = "shot";
    shotIr.in = sequenceLeaf;
    CHECK_NOTHROW(validate(shotIr, loaded));

    Ir versionBySequence;
    versionBySequence.op = Op::Find;
    versionBySequence.entity = "version";
    versionBySequence.in = sequenceLeaf;
    CHECK_NOTHROW(validate(versionBySequence, loaded));

    auto shotLeaf = parentIdLeaf("shot", 1);
    Ir versionByShot;
    versionByShot.op = Op::Find;
    versionByShot.entity = "version";
    versionByShot.in = shotLeaf;
    CHECK_NOTHROW(validate(versionByShot, loaded));

    auto assetLeaf = parentIdLeaf("asset", 1);
    Ir versionByAsset;
    versionByAsset.op = Op::Find;
    versionByAsset.entity = "version";
    versionByAsset.in = assetLeaf;
    CHECK_NOTHROW(validate(versionByAsset, loaded));

    auto playlistLeaf = parentIdLeaf("playlist", 1);
    Ir versionByPlaylist;
    versionByPlaylist.op = Op::Find;
    versionByPlaylist.entity = "version";
    versionByPlaylist.in = playlistLeaf;
    CHECK_NOTHROW(validate(versionByPlaylist, loaded));

    auto inheritedProject = parentIdLeaf("project", 1);
    Ir versionByProject;
    versionByProject.op = Op::Find;
    versionByProject.entity = "version";
    versionByProject.in = inheritedProject;
    CHECK_NOTHROW(validate(versionByProject, loaded));
}

TEST_CASE("validate rejects an illegal in parent") {
    const Schema loaded = schema();
    auto leaf = std::make_shared<ExprNode>();
    leaf->op = ExprOp::Eq;
    leaf->kind = ExprKind::Field;
    leaf->parent = "episode";
    leaf->key = "id";
    leaf->value = std::int64_t{1};
    Ir ir;
    ir.op = Op::Find;
    ir.entity = "version";
    ir.in = leaf;
    CHECK_THROWS_WITH_AS(validate(ir, loaded), "in: parent \"episode\" is not in scope", Error);
}

TEST_CASE("validate rejects a zero limit and a cursor without limit") {
    const Schema loaded = schema();
    Ir ir;
    ir.op = Op::Find;
    ir.entity = "project";
    ir.limit = 0;
    CHECK_THROWS_WITH_AS(validate(ir, loaded), "limit must be greater than zero", Error);

    ir.limit = std::nullopt;
    ir.cursor = std::string("5");
    CHECK_THROWS_WITH_AS(validate(ir, loaded), "cursor requires a limit", Error);

    ir.limit = 25;
    CHECK_NOTHROW(validate(ir, loaded));
}

TEST_CASE("validate accepts count and rejects its pagination") {
    const Schema loaded = schema();
    Ir ir;
    ir.op = Op::Count;
    ir.entity = "sequence";
    CHECK_NOTHROW(validate(ir, loaded));

    ir.limit = 10;
    CHECK_THROWS_WITH_AS(validate(ir, loaded), "count does not accept a limit", Error);

    ir.limit = std::nullopt;
    ir.cursor = std::string("5");
    CHECK_THROWS_WITH_AS(validate(ir, loaded), "count does not accept a cursor", Error);
}

TEST_CASE("validate accepts a known fields projection") {
    const Schema loaded = schema();
    Ir ir;
    ir.op = Op::Find;
    ir.entity = "version";
    ir.fields = std::vector<std::string>{"id", "name", "author", "image"};
    CHECK_NOTHROW(validate(ir, loaded));
}

TEST_CASE("validate rejects an empty or unknown fields projection") {
    const Schema loaded = schema();
    Ir ir;
    ir.op = Op::Find;
    ir.entity = "version";
    ir.fields = std::vector<std::string>{};
    CHECK_THROWS_WITH_AS(validate(ir, loaded), "fields must list at least one key", Error);

    ir.fields = std::vector<std::string>{"id", "not_a_field"};
    CHECK_THROWS_WITH_AS(
        validate(ir, loaded), "fields: unknown field \"not_a_field\"", Error);
}

TEST_CASE("a returned record must carry id and name") {
    Record good;
    good.fields["id"] = std::int64_t{12};
    good.fields["name"] = std::string("paw3");
    CHECK_NOTHROW(checkRecord(good));

    Record missingId = good;
    missingId.fields.erase("id");
    CHECK_THROWS_WITH_AS(checkRecord(missingId), "record missing \"id\"", Error);

    Record badId = good;
    badId.fields["id"] = std::string("12");
    CHECK_THROWS_WITH_AS(checkRecord(badId), "record \"id\" is not an int", Error);

    Record missingName = good;
    missingName.fields.erase("name");
    CHECK_THROWS_WITH_AS(checkRecord(missingName), "record missing \"name\"", Error);

    Record badName = good;
    badName.fields["name"] = std::int64_t{1};
    CHECK_THROWS_WITH_AS(checkRecord(badName), "record \"name\" is not a string", Error);
}
