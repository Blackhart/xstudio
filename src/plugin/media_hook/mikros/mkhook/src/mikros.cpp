// SPDX-License-Identifier: Apache-2.0
#include <cstdlib>

#include "xstudio/media_hook/media_hook.hpp"
#include "xstudio/utility/helpers.hpp"
#include "xstudio/utility/json_store.hpp"

using namespace xstudio;
using namespace xstudio::media_hook;
using namespace xstudio::utility;

/*
Mikros media hook — colour management.

One xStudio process = one show: $OCIO is set at app start and points to that
show's OCIO config. Optional env overrides:
  REVIEW_ACTIVE_DISPLAYS — colon-separated displays (first = default)
  DEFAULT_VIEW           — preferred view (automatic_view; needs Global View OFF)
*/
class MikrosMediaHook : public MediaHook {
  public:
    MikrosMediaHook() : MediaHook("Mikros") {}
    ~MikrosMediaHook() override = default;

    utility::JsonStore modify_metadata(
        const utility::MediaReference &mr, const utility::JsonStore &metadata) override {
        utility::JsonStore result = metadata;

        const caf::uri &uri =
            mr.container() or mr.uris().empty() ? mr.uri() : mr.uris()[0].first;

        const std::string path            = to_string(uri);
        result["colour_pipeline"]         = colour_params(path, metadata);
        // Path helps OCIO file rules when present in the show config.
        result["colour_pipeline"]["path"] = path;
        return result;
    }

    utility::JsonStore
    colour_params(const std::string & /*path*/, const utility::JsonStore &metadata) {
        utility::JsonStore r(R"({})"_json);

        // Prefer an explicit config already on the media metadata. Otherwise
        // omit ocio_config so OCIOColourPipeline uses getenv("OCIO") / prefs.
        try {
            if (metadata.contains("colour_pipeline") and
                metadata.at("colour_pipeline").contains("ocio_config") and
                not metadata.at("colour_pipeline").at("ocio_config").is_null()) {
                const auto &cfg = metadata.at("colour_pipeline").at("ocio_config");
                if (cfg.is_string() and not cfg.get<std::string>().empty() and
                    cfg.get<std::string>() != "__raw__") {
                    r["ocio_config"] = cfg;
                }
            }
        } catch (...) {
        }

        // Filter selectable displays; first entry present in the OCIO config
        // becomes the default display via setActiveDisplays.
        if (const char *active_displays = std::getenv("REVIEW_ACTIVE_DISPLAYS");
            active_displays and *active_displays) {
            r["active_displays"] = active_displays;
        }

        // Preferred view when Global View mode is OFF (automatic per-media).
        if (const char *default_view = std::getenv("DEFAULT_VIEW");
            default_view and *default_view) {
            r["automatic_view"] = default_view;
        }

        return r;
    }
};

extern "C" {
plugin_manager::PluginFactoryCollection *plugin_factory_collection_ptr() {
    return new plugin_manager::PluginFactoryCollection(
        std::vector<std::shared_ptr<plugin_manager::PluginFactory>>(
            {std::make_shared<MediaHookPlugin<MediaHookActor<MikrosMediaHook>>>(
                Uuid("a7b3c1d2-4e5f-6a7b-8c9d-0e1f2a3b4c5d"),
                "Mikros",
                "Mikros",
                "Mikros Media Hook",
                semver::version("1.0.0"))}));
}
}
