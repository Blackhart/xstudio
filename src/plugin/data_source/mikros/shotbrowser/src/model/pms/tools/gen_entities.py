#!/usr/bin/env python3
"""Generates entity proxies and query factories from the domain schema.

The generated headers are build outputs. Edit schema.yaml, not the headers.
YAML `scope` lists direct parents only. Ancestors are inherited for In().
"""

import sys
from pathlib import Path

import yaml

TYPE_CPP = {
    "string": "std::string",
    "int": "std::int64_t",
    "date": "Date",
}


def camel(name):
    """Converts a YAML key to a C++ field identifier.

    Args:
        name: Snake-case field key from the schema.

    Returns:
        The camelCase identifier. `start_date` becomes `startDate`.
    """
    parts = name.split("_")
    return parts[0] + "".join(part[:1].upper() + part[1:] for part in parts[1:])


def pascal(name):
    """Converts a YAML entity key to a C++ type name.

    Args:
        name: Snake-case entity key from the schema.

    Returns:
        The PascalCase name. `shot_group` becomes `ShotGroup`.
    """
    return "".join(part[:1].upper() + part[1:] for part in name.split("_"))


def file_banner():
    """Returns the banner shared by both generated headers.

    Returns:
        Lines that mark the file as generated and describe it for Doxygen.
    """
    return [
        "// generated, do not edit",
        "/**",
        " * @file",
        " * @brief Generated from schema.yaml. Do not edit.",
        " */",
    ]


def expanded_scope(entity_key, entities):
    """Returns direct parents plus ancestors, farthest first.

    Args:
        entity_key: YAML entity name.
        entities: Map of entity name to YAML body.

    Returns:
        Parent keys legal in In() for this entity.
    """
    out = []
    seen = set()

    def walk(key):
        for parent in entities[key].get("scope") or []:
            walk(parent)
            if parent not in seen:
                seen.add(parent)
                out.append(parent)

    walk(entity_key)
    return out


def main():
    """Writes entities.hpp and query_factories.hpp for the schema path.

    Args:
        sys.argv[1]: Path to schema.yaml.
        sys.argv[2]: Directory that receives the two headers.
    """
    schema_path = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    data = yaml.safe_load(schema_path.read_text())
    out_dir.mkdir(parents=True, exist_ok=True)
    entities_yaml = data["entities"]

    entities = file_banner() + ["#pragma once", '#include "pms/expr.hpp"', "", "namespace pms {", ""]
    factories = file_banner() + ["#pragma once", "", "namespace pms {", ""]

    scope_parents = []
    seen_parents = set()

    for key, spec in entities_yaml.items():
        struct = pascal(key)
        scope = expanded_scope(key, entities_yaml)
        scope_types = ", ".join(pascal(parent) for parent in scope)
        entities.extend(
            [
                "/**",
                f" * @brief Domain tag for `{key}`.",
                " *",
                f" * Call {struct}() to start a query. That function hides this struct in",
                f" * ordinary lookup. decltype({struct}()) names the query type.",
                " */",
                f"struct {struct} {{",
                f"    /// YAML entity name stored on the intermediate representation.",
                f'    static constexpr const char* entityName = "{key}";',
            ]
        )
        for field, meta in spec["fields"].items():
            cpp_type = TYPE_CPP[meta["type"]]
            ident = camel(field)
            entities.append(f"    /// Field `{field}`, type `{meta['type']}`.")
            entities.append(
                f'    static constexpr Field<{struct}, {cpp_type}> {ident}{{"{field}"}};'
            )
        entities.append(
            "    /// Legal parents for In(), including inherited ancestors."
        )
        entities.append(f"    using Scope = ScopeList<{scope_types}>;")
        entities.extend(
            [
                "};",
                "",
                "/**",
                f" * @brief Field proxies for `{key}`.",
                " *",
                f" * Bring them into scope with `using namespace pms::{key}`.",
                " */",
                f"namespace {key} {{",
            ]
        )
        for field in spec["fields"]:
            ident = camel(field)
            entities.append(f"    /// Proxy for `{field}`.")
            entities.append(f"    inline constexpr auto {ident} = {struct}::{ident};")
        entities.extend(["}", ""])
        for parent in scope:
            if parent not in seen_parents:
                seen_parents.add(parent)
                scope_parents.append(parent)
        factories.extend(
            [
                "/**",
                f" * @brief Starts a `{key}` query.",
                " *",
                " * @return An empty query. find() with no In/Where lists every row.",
                " */",
                f"inline auto {struct}() -> Query<struct {struct}> {{",
                "    return {};",
                "}",
                "",
            ]
        )

    entities.extend(
        [
            "/**",
            " * @brief Parent proxies for In().",
            " *",
            " * Named apart from field namespaces (`pms::project`, …) because a",
            " * variable and a namespace cannot share a name in C++.",
            " */",
            "namespace scope {",
            "",
        ]
    )
    for parent in scope_parents:
        struct = pascal(parent)
        ident = camel(parent)
        entities.extend(
            [
                "/**",
                f" * @brief Scope proxy for parent `{parent}` in In().",
                " */",
                f'inline constexpr ScopeRef<{struct}> {ident}{{"{parent}"}};',
                "",
            ]
        )
    entities.extend(["}", "", "}", ""])
    factories.extend(["}", ""])

    (out_dir / "entities.hpp").write_text("\n".join(entities))
    (out_dir / "query_factories.hpp").write_text("\n".join(factories))


if __name__ == "__main__":
    main()
