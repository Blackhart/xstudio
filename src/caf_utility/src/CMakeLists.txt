find_package(fmt REQUIRED)
find_package(Imath REQUIRED)

SET(LINK_DEPS
	CAF::io
	CAF::core
	fmt::fmt
	Imath::Imath
)

create_component(caf_utility ${XSTUDIO_GLOBAL_VERSION} "${LINK_DEPS}")