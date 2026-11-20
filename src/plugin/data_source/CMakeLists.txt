
if("${STUDIO_PLUGINS}" STREQUAL "mikros")
	# Shared lib first so shotbrowser (and later plugins) can link pms::pms.
	add_subdirectory(mikros/pms)
	add_src_and_test(mikros/shotbrowser)
else()
	build_studio_plugins("${STUDIO_PLUGINS}")
endif()

# Uncomment the line below to build the demo plugin
# build_studio_plugins("demo")
