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
}

TEST_CASE("validate rejects a broken tree") {
    const Schema loaded = schema();

    Ir bare;
    bare.entity = "project";
    bare.where = fieldIr("status", ExprOp::Eq, std::string("ip")).where;
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
    group->args = {fieldIr("status", ExprOp::Eq, std::string("ip")).where,
                   fieldIr("name", ExprOp::Eq, std::string("paw")).where};
    Ir badNot;
    badNot.op = Op::Find;
    badNot.entity = "project";
    badNot.where = group;
    CHECK_THROWS_WITH_AS(validate(badNot, loaded), "where: not expects one child", Error);
}

TEST_CASE("validate accepts sequence, shot, asset, and inherited version in clauses") {
    const Schema loaded = schema();

    auto projectLeaf = std::make_shared<ExprNode>();
    projectLeaf->op = ExprOp::Eq;
    projectLeaf->kind = ExprKind::Scope;
    projectLeaf->key = "project";
    projectLeaf->value = std::int64_t{1};
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

    auto sequenceLeaf = std::make_shared<ExprNode>();
    sequenceLeaf->op = ExprOp::Eq;
    sequenceLeaf->kind = ExprKind::Scope;
    sequenceLeaf->key = "sequence";
    sequenceLeaf->value = std::int64_t{1};
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

    auto shotLeaf = std::make_shared<ExprNode>();
    shotLeaf->op = ExprOp::Eq;
    shotLeaf->kind = ExprKind::Scope;
    shotLeaf->key = "shot";
    shotLeaf->value = std::int64_t{1};
    Ir versionByShot;
    versionByShot.op = Op::Find;
    versionByShot.entity = "version";
    versionByShot.in = shotLeaf;
    CHECK_NOTHROW(validate(versionByShot, loaded));

    auto assetLeaf = std::make_shared<ExprNode>();
    assetLeaf->op = ExprOp::Eq;
    assetLeaf->kind = ExprKind::Scope;
    assetLeaf->key = "asset";
    assetLeaf->value = std::int64_t{1};
    Ir versionByAsset;
    versionByAsset.op = Op::Find;
    versionByAsset.entity = "version";
    versionByAsset.in = assetLeaf;
    CHECK_NOTHROW(validate(versionByAsset, loaded));

    auto playlistLeaf = std::make_shared<ExprNode>();
    playlistLeaf->op = ExprOp::Eq;
    playlistLeaf->kind = ExprKind::Scope;
    playlistLeaf->key = "playlist";
    playlistLeaf->value = std::int64_t{1};
    Ir versionByPlaylist;
    versionByPlaylist.op = Op::Find;
    versionByPlaylist.entity = "version";
    versionByPlaylist.in = playlistLeaf;
    CHECK_NOTHROW(validate(versionByPlaylist, loaded));

    auto inheritedProject = std::make_shared<ExprNode>();
    inheritedProject->op = ExprOp::Eq;
    inheritedProject->kind = ExprKind::Scope;
    inheritedProject->key = "project";
    inheritedProject->value = std::int64_t{1};
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
    leaf->kind = ExprKind::Scope;
    leaf->key = "episode";
    leaf->value = std::string("ep01");
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
