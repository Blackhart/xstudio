# Builds justinfx/gofileseq (C++) and exposes fileseq::fileseq for ShotGrid
# sequence paths (`%04d`, `####`, `@` → xSTUDIO `{:04d}`).

include(ExternalProject)

set(FILESEQ_PREFIX "${CMAKE_BINARY_DIR}/_deps/fileseq")
set(FILESEQ_INSTALL "${FILESEQ_PREFIX}/install")
set(FILESEQ_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

ExternalProject_Add(
	gofileseq
	GIT_REPOSITORY https://github.com/justinfx/gofileseq.git
	GIT_TAG v3.2.0
	GIT_SHALLOW TRUE
	PREFIX "${FILESEQ_PREFIX}"
	SOURCE_SUBDIR cpp
	CMAKE_ARGS
		-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON
		-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
		-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
	BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target fileseq_static
	INSTALL_COMMAND
		${CMAKE_COMMAND}
			-DINSTALL_PREFIX=${FILESEQ_INSTALL}
			-DBINARY_DIR=<BINARY_DIR>
			-DSOURCE_DIR=<SOURCE_DIR>
			-P ${FILESEQ_CMAKE_DIR}/install_fileseq.cmake
	BUILD_BYPRODUCTS "${FILESEQ_INSTALL}/lib/libfileseq.a"
)

file(MAKE_DIRECTORY "${FILESEQ_INSTALL}/include" "${FILESEQ_INSTALL}/lib")
add_library(fileseq::fileseq STATIC IMPORTED GLOBAL)
add_dependencies(fileseq::fileseq gofileseq)
set_target_properties(fileseq::fileseq PROPERTIES
	IMPORTED_LOCATION "${FILESEQ_INSTALL}/lib/libfileseq.a"
	INTERFACE_INCLUDE_DIRECTORIES "${FILESEQ_INSTALL}/include"
)
if(UNIX)
	set_property(TARGET fileseq::fileseq APPEND PROPERTY INTERFACE_LINK_LIBRARIES pthread)
endif()
