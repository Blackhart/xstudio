// SPDX-License-Identifier: Apache-2.0
/**
 * @file schema.cpp
 * @brief Loads entity and field rules from the domain YAML.
 */

#include "pms/schema.hpp"

#include "pms/error.hpp"

#include <unordered_set>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace pms {
namespace {

/**
 * @brief Reads the operators declared for a YAML value type.
 * @param types `types` map from the schema file.
 * @param typeName Type name stored on a field, such as `"date"`.
 * @return Operator names, in file order.
 * @throw Error When the type is missing or declares no operators.
 */
std::vector<std::string> loadOps(const YAML::Node &types, const std::string &typeName) {
    const YAML::Node type = types[typeName];
    if (!type || !type["ops"]) {
        throw Error("schema: unknown type \"" + typeName + "\"");
    }
    std::vector<std::string> ops;
    for (YAML::const_iterator op = type["ops"].begin(); op != type["ops"].end(); ++op) {
        ops.push_back(op->as<std::string>());
    }
    if (ops.empty()) {
        throw Error("schema: type \"" + typeName + "\" has no operators");
    }
    return ops;
}

} // namespace

const FieldSpec *EntitySpec::field(const std::string &key) const {
    for (const FieldSpec &spec : fields) {
        if (spec.key == key) {
            return &spec;
        }
    }
    return nullptr;
}

Schema Schema::load(const std::string &path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception &ex) {
        throw Error(std::string("schema: ") + ex.what());
    }
    if (!root["types"] || !root["entities"]) {
        throw Error("schema: missing types or entities");
    }

    Schema schema;
    const YAML::Node entities = root["entities"];
    for (YAML::const_iterator it = entities.begin(); it != entities.end(); ++it) {
        EntitySpec entity;
        entity.name = it->first.as<std::string>();
        const YAML::Node body = it->second;
        entity.creatable = body["creatable"].as<bool>(false);
        if (body["scope"]) {
            for (YAML::const_iterator parent = body["scope"].begin(); parent != body["scope"].end();
                 ++parent) {
                entity.scope.push_back(parent->as<std::string>());
            }
        }
        if (!body["fields"]) {
            throw Error("schema: entity \"" + entity.name + "\" has no fields");
        }
        for (YAML::const_iterator fieldEntry = body["fields"].begin();
             fieldEntry != body["fields"].end();
             ++fieldEntry) {
            FieldSpec spec;
            spec.key = fieldEntry->first.as<std::string>();
            const YAML::Node meta = fieldEntry->second;
            spec.type = meta["type"].as<std::string>();
            spec.filter = meta["filter"].as<bool>(false);
            spec.canSet = meta["set"].as<bool>(false);
            spec.requiredOnCreate = meta["required_on_create"].as<bool>(false);
            spec.ops = loadOps(root["types"], spec.type);
            entity.fields.push_back(std::move(spec));
        }
        schema.entities_.push_back(std::move(entity));
    }
    if (schema.entities_.empty()) {
        throw Error("schema: no entities");
    }
    return schema;
}

const EntitySpec *Schema::entity(const std::string &name) const {
    for (const EntitySpec &spec : entities_) {
        if (spec.name == name) {
            return &spec;
        }
    }
    return nullptr;
}

bool Schema::allowsInParent(const EntitySpec &entity, const std::string &parent) const {
    std::unordered_set<std::string> visited;
    std::vector<std::string> stack = entity.scope;
    while (!stack.empty()) {
        const std::string current = std::move(stack.back());
        stack.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        if (current == parent) {
            return true;
        }
        const EntitySpec *spec = this->entity(current);
        if (!spec) {
            continue;
        }
        for (const std::string &ancestor : spec->scope) {
            stack.push_back(ancestor);
        }
    }
    return false;
}

} // namespace pms
