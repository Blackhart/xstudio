# Copies the built static lib and public headers into INSTALL_PREFIX.
# Used as the ExternalProject INSTALL_COMMAND for justinfx/gofileseq (cpp/).
#
# Expected -D variables:
#   INSTALL_PREFIX  install root (…/include/fileseq, …/lib)
#   BINARY_DIR      gofileseq cmake build dir (contains libfileseq.a)
#   SOURCE_DIR      gofileseq git checkout root (headers live under cpp/)

if(NOT INSTALL_PREFIX OR NOT BINARY_DIR OR NOT SOURCE_DIR)
	message(FATAL_ERROR "install_fileseq.cmake needs INSTALL_PREFIX, BINARY_DIR, and SOURCE_DIR")
endif()

set(_cpp "${SOURCE_DIR}/cpp")
set(_lib "${BINARY_DIR}/libfileseq.a")
if(NOT EXISTS "${_lib}")
	message(FATAL_ERROR "libfileseq.a not found at ${_lib}")
endif()

file(MAKE_DIRECTORY "${INSTALL_PREFIX}/lib")
file(MAKE_DIRECTORY "${INSTALL_PREFIX}/include/fileseq/ranges")

file(COPY "${_lib}" DESTINATION "${INSTALL_PREFIX}/lib")
foreach(_header error.h fileseq.h frameset.h pad.h sequence.h)
	file(COPY "${_cpp}/${_header}" DESTINATION "${INSTALL_PREFIX}/include/fileseq")
endforeach()
file(COPY "${_cpp}/ranges/ranges.h" DESTINATION "${INSTALL_PREFIX}/include/fileseq/ranges")
