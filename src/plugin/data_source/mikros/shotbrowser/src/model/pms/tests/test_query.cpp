// SPDX-License-Identifier: Apache-2.0

#include "pms/pms.hpp"

#include <doctest/doctest.h>

using namespace pms;
using namespace pms::project;

TEST_CASE("where builds a stable tree") {
    const Ir ir = Project()
                      .Where((code == "PAW01" || code == "PAW02") && name != "archive" &&
                             !(name == "OLD"))
                      .ir();

    const char *expected = R"(op:
entity: project
in:
where:
  and
    and
      or
        field code eq "PAW01"
        field code eq "PAW02"
      field name ne "archive"
    not
      field name eq "OLD"
)";
    CHECK(dump(ir) == expected);
    CHECK_FALSE(ir.op.has_value());
}

TEST_CASE("two where clauses combine with and") {
    const std::string split = dump(Project().Where(code == "PAW01").Where(name != "archive").ir());
    const std::string once = dump(Project().Where(code == "PAW01" && name != "archive").ir());
    CHECK(split == once);

    const char *nested = R"(op:
entity: project
in:
where:
  and
    and
      field code eq "PAW01"
      field name ne "archive"
    field name eq "NEW"
)";
    CHECK(dump(Project().Where(code == "PAW01").Where(name != "archive").Where(name == "NEW").ir()) ==
          nested);
}

TEST_CASE("an iso date parses into a calendar day") {
    CHECK(parseDate("2024-02-29") == Date{2024, 2, 29});
    CHECK_THROWS_AS(parseDate("2026-02-29"), Error);
    CHECK_THROWS_AS(parseDate("2026-13-01"), Error);
    CHECK_THROWS_AS(parseDate("not-a-date"), Error);
}

TEST_CASE("a hand built tree dumps the same way") {
    auto code = std::make_shared<ExprNode>();
    code->op = ExprOp::Eq;
    code->kind = ExprKind::Field;
    code->key = "code";
    code->value = std::string("PAW01");

    auto name = std::make_shared<ExprNode>();
    name->op = ExprOp::Ne;
    name->kind = ExprKind::Field;
    name->key = "name";
    name->value = std::string("archive");

    auto both = std::make_shared<ExprNode>();
    both->op = ExprOp::And;
    both->kind = ExprKind::Group;
    both->args = {code, name};

    Ir ir;
    ir.entity = "project";
    ir.where = both;

    const char *expected = R"(op:
entity: project
in:
where:
  and
    field code eq "PAW01"
    field name ne "archive"
)";
    CHECK(dump(ir) == expected);
}

TEST_CASE("find without a where lists every project") {
    const char *expected = R"(op:
entity: project
in:
where:
)";
    CHECK(dump(Project().ir()) == expected);
}

TEST_CASE("count dump names the verb") {
    Ir ir = Sequence().In(scope::project == std::int64_t{1}).ir();
    ir.op = Op::Count;
    const char *expected = R"(op: count
entity: sequence
in:
  scope project eq 1
where:
)";
    CHECK(dump(ir) == expected);
}

TEST_CASE("in builds a scope leaf on sequence by project id") {
    const char *expected = R"(op:
entity: sequence
in:
  scope project eq 1
where:
)";
    CHECK(dump(Sequence().In(scope::project == std::int64_t{1}).ir()) == expected);
}

TEST_CASE("in builds a scope leaf on version by sequence id") {
    const char *expected = R"(op:
entity: version
in:
  scope sequence eq 7
where:
)";
    CHECK(dump(Version().In(scope::sequence == std::int64_t{7}).ir()) == expected);
}

TEST_CASE("in builds a scope leaf on shot by sequence id") {
    const char *expected = R"(op:
entity: shot
in:
  scope sequence eq 3
where:
)";
    CHECK(dump(Shot().In(scope::sequence == std::int64_t{3}).ir()) == expected);
}

TEST_CASE("in builds a scope leaf on version by shot id") {
    const char *expected = R"(op:
entity: version
in:
  scope shot eq 9
where:
)";
    CHECK(dump(Version().In(scope::shot == std::int64_t{9}).ir()) == expected);
}

TEST_CASE("in builds a scope leaf on asset by project id") {
    const char *expected = R"(op:
entity: asset
in:
  scope project eq 1
where:
)";
    CHECK(dump(Asset().In(scope::project == std::int64_t{1}).ir()) == expected);
}

TEST_CASE("in builds a scope leaf on version by asset id") {
    const char *expected = R"(op:
entity: version
in:
  scope asset eq 4
where:
)";
    CHECK(dump(Version().In(scope::asset == std::int64_t{4}).ir()) == expected);
}

TEST_CASE("in builds a scope leaf on playlist by project id") {
    const char *expected = R"(op:
entity: playlist
in:
  scope project eq 1
where:
)";
    CHECK(dump(Playlist().In(scope::project == std::int64_t{1}).ir()) == expected);
}

TEST_CASE("in builds a scope leaf on version by playlist id") {
    const char *expected = R"(op:
entity: version
in:
  scope playlist eq 5
where:
)";
    CHECK(dump(Version().In(scope::playlist == std::int64_t{5}).ir()) == expected);
}
