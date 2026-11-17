// SPDX-License-Identifier: Apache-2.0
/**
 * @file shotgrid/mapping.cpp
 * @brief Loads domain ↔ ShotGrid names from YAML.
 */

#include "pms/shotgrid/mapping.hpp"

#include "pms/error.hpp"

#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

namespace pms {

const ShotGridSchemaField *ShotGridSchema::findField(const std::string &key) const {
    for (const ShotGridSchemaField &spec : fields) {
        if (spec.domain == key) {
            return &spec;
        }
    }
    return nullptr;
}

const ShotGridSchemaLink *ShotGridSchema::findLink(const std::string &key) const {
    for (const ShotGridSchemaLink &spec : links) {
        if (spec.domain == key) {
            return &spec;
        }
    }
    return nullptr;
}

ShotGridMapping ShotGridMapping::parse(const std::string &yaml) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml);
    } catch (const YAML::Exception &ex) {
        throw Error(std::string("shotgrid mapping: ") + ex.what());
    }
    if (!root["entities"]) {
        throw Error("shotgrid mapping: missing entities");
    }

    ShotGridMapping mapping;
    const YAML::Node entities = root["entities"];
    for (YAML::const_iterator it = entities.begin(); it != entities.end(); ++it) {
        ShotGridSchema schema;
        schema.domainName = it->first.as<std::string>();
        const YAML::Node body = it->second;
        if (!body["shotgrid"]) {
            throw Error("shotgrid mapping: entity \"" + schema.domainName + "\" missing shotgrid");
        }
        schema.shotgridName = body["shotgrid"].as<std::string>("");
        if (!body["fields"]) {
            throw Error("shotgrid mapping: entity \"" + schema.domainName + "\" has no fields");
        }
        for (YAML::const_iterator fieldEntry = body["fields"].begin();
             fieldEntry != body["fields"].end();
             ++fieldEntry) {
            ShotGridSchemaField field;
            field.domain = fieldEntry->first.as<std::string>();
            const YAML::Node fieldBody = fieldEntry->second;
            if (!fieldBody["shotgrid"]) {
                throw Error(
                    "shotgrid mapping: field \"" + schema.domainName + "." + field.domain +
                    "\" missing shotgrid");
            }
            field.shotgrid = fieldBody["shotgrid"].as<std::string>("");
            if (fieldBody["provider"]) {
                field.provider = fieldBody["provider"].as<std::string>("");
            }
            schema.fields.push_back(std::move(field));
        }
        if (body["links"]) {
            for (YAML::const_iterator linkEntry = body["links"].begin();
                 linkEntry != body["links"].end();
                 ++linkEntry) {
                ShotGridSchemaLink link;
                link.domain = linkEntry->first.as<std::string>();
                const YAML::Node linkBody = linkEntry->second;
                if (!linkBody["shotgrid"] || !linkBody["target"]) {
                    throw Error(
                        "shotgrid mapping: link \"" + schema.domainName + "." + link.domain +
                        "\" needs shotgrid and target");
                }
                link.shotgrid = linkBody["shotgrid"].as<std::string>("");
                link.shotgridTargetName = linkBody["target"].as<std::string>();
                if (link.shotgridTargetName.empty()) {
                    throw Error(
                        "shotgrid mapping: link \"" + schema.domainName + "." + link.domain +
                        "\" has empty target");
                }
                schema.links.push_back(std::move(link));
            }
        }
        mapping.entities_.push_back(std::move(schema));
    }
    return mapping;
}

ShotGridMapping ShotGridMapping::load(const std::string &path) {
    std::ifstream input(path);
    if (!input) {
        throw Error("shotgrid mapping: cannot open \"" + path + "\"");
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parse(buffer.str());
}

const ShotGridSchema *ShotGridMapping::findSchema(const std::string &domainName) const {
    for (const ShotGridSchema &schema : entities_) {
        if (schema.domainName == domainName) {
            return &schema;
        }
    }
    return nullptr;
}

void ShotGridMapping::throwIfNamesMissing() const {
    for (const ShotGridSchema &schema : entities_) {
        if (schema.shotgridName.empty()) {
            throw Error("shotgrid mapping: entity \"" + schema.domainName + "\" has empty shotgrid");
        }
        for (const ShotGridSchemaField &field : schema.fields) {
            if (field.shotgrid.empty()) {
                throw Error(
                    "shotgrid mapping: field \"" + schema.domainName + "." + field.domain +
                    "\" has empty shotgrid");
            }
        }
        for (const ShotGridSchemaLink &link : schema.links) {
            if (link.shotgrid.empty()) {
                throw Error(
                    "shotgrid mapping: link \"" + schema.domainName + "." + link.domain +
                    "\" has empty shotgrid");
            }
            if (!this->findSchema(link.shotgridTargetName)) {
                throw Error(
                    "shotgrid mapping: link \"" + schema.domainName + "." + link.domain +
                    "\" targets unknown entity \"" + link.shotgridTargetName + "\"");
            }
        }
    }
}

} // namespace pms
