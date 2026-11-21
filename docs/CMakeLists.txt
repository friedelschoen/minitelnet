# Invoked from the project's root via add_subdirectory(docs).
find_package(Doxygen REQUIRED)
find_program(SPHINX_EXECUTABLE NAMES sphinx-build REQUIRED)

set(DOXYGEN_PROJECT_NAME "minitelnet")
set(DOXYGEN_PROJECT_BRIEF "Small TELNET protocol implementation for C")
set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/doxygen")
set(DOXYGEN_GENERATE_HTML NO)
set(DOXYGEN_GENERATE_XML YES)
set(DOXYGEN_GENERATE_MAN NO)
set(DOXYGEN_EXTRACT_ALL NO)
set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
set(DOXYGEN_QUIET YES)

# Doxygen XML, not Doxygen HTML, is the input to Breathe.
doxygen_add_docs(minitelnet_doxygen
    "${PROJECT_SOURCE_DIR}/minitelnet.h"
    "${PROJECT_SOURCE_DIR}/minitelnet-negotiation.h"
    "${PROJECT_SOURCE_DIR}/minitelnet-def.h"
    COMMENT "Generate minitelnet Doxygen XML"
)

add_custom_target(minitelnet_docs ALL
    COMMAND "${CMAKE_COMMAND}" -E env
            "MINITELNET_DOXYGEN_XML=${CMAKE_CURRENT_BINARY_DIR}/doxygen/xml"
            "${SPHINX_EXECUTABLE}" -b html
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/html"
    DEPENDS minitelnet_doxygen
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generate minitelnet Sphinx HTML documentation"
    VERBATIM
)
