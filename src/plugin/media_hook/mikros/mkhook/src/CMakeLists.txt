SET(LINK_DEPS
	xstudio::media_hook
	xstudio::utility
	xstudio::module
)

create_plugin_with_alias(media_hook_mikros xstudio::media_hook::mkhook ${XSTUDIO_GLOBAL_VERSION}  "${LINK_DEPS}")

add_preference(plugin_mikros_media_hook.json ${CMAKE_CURRENT_SOURCE_DIR} media_hook_mikros)
