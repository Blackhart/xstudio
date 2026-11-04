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

const ShotGridFieldMap *ShotGridEntityMap::field(const std::string &key) const {
    for (const ShotGridFieldMap &spec : fields) {
        if (spec.domain == key) {
            return &spec;
        }
    }
    return nullptr;
}

const ShotGridLinkMap *ShotGridEntityMap::link(const std::string &key) const {
    for (const ShotGridLinkMap &spec : links) {
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
        ShotGridEntityMap entity;
        entity.domain = it->first.as<std::string>();
        const YAML::Node body = it->second;
        if (!body["shotgrid"]) {
            throw Error("shotgrid mapping: entity \"" + entity.domain + "\" missing shotgrid");
        }
        entity.shotgrid = body["shotgrid"].as<std::string>("");
        if (!body["fields"]) {
            throw Error("shotgrid mapping: entity \"" + entity.domain + "\" has no fields");
        }
        for (YAML::const_iterator fieldEntry = body["fields"].begin();
             fieldEntry != body["fields"].end();
             ++fieldEntry) {
            ShotGridFieldMap field;
            field.domain = fieldEntry->first.as<std::string>();
            const YAML::Node fieldBody = fieldEntry->second;
            if (!fieldBody["shotgrid"]) {
                throw Error(
                    "shotgrid mapping: field \"" + entity.domain + "." + field.domain +
                    "\" missing shotgrid");
            }
            field.shotgrid = fieldBody["shotgrid"].as<std::string>("");
            entity.fields.push_back(std::move(field));
        }
        if (body["links"]) {
            for (YAML::const_iterator linkEntry = body["links"].begin();
                 linkEntry != body["links"].end();
                 ++linkEntry) {
                ShotGridLinkMap link;
                link.domain = linkEntry->first.as<std::string>();
                const YAML::Node linkBody = linkEntry->second;
                if (!linkBody["shotgrid"] || !linkBody["target"]) {
                    throw Error(
                        "shotgrid mapping: link \"" + entity.domain + "." + link.domain +
                        "\" needs shotgrid and target");
                }
                link.shotgrid = linkBody["shotgrid"].as<std::string>("");
                link.target = linkBody["target"].as<std::string>();
                if (link.target.empty()) {
                    throw Error(
                        "shotgrid mapping: link \"" + entity.domain + "." + link.domain +
                        "\" has empty target");
                }
                entity.links.push_back(std::move(link));
            }
        }
        mapping.entities_.push_back(std::move(entity));
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

const ShotGridEntityMap *ShotGridMapping::entity(const std::string &name) const {
    for (const ShotGridEntityMap &spec : entities_) {
        if (spec.domain == name) {
            return &spec;
        }
    }
    return nullptr;
}

void ShotGridMapping::requireComplete() const {
    for (const ShotGridEntityMap &entity : entities_) {
        if (entity.shotgrid.empty()) {
            throw Error("shotgrid mapping: entity \"" + entity.domain + "\" has empty shotgrid");
        }
        for (const ShotGridFieldMap &field : entity.fields) {
            if (field.shotgrid.empty()) {
                throw Error(
                    "shotgrid mapping: field \"" + entity.domain + "." + field.domain +
                    "\" has empty shotgrid");
            }
        }
        for (const ShotGridLinkMap &link : entity.links) {
            if (link.shotgrid.empty()) {
                throw Error(
                    "shotgrid mapping: link \"" + entity.domain + "." + link.domain +
                    "\" has empty shotgrid");
            }
            if (!this->entity(link.target)) {
                throw Error(
                    "shotgrid mapping: link \"" + entity.domain + "." + link.domain +
                    "\" targets unknown entity \"" + link.target + "\"");
            }
        }
    }
}

} // namespace pms
