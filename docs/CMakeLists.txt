set(DOXYGEN_PROJECT_NAME "minitelnet")
set(DOXYGEN_PROJECT_BRIEF "Small TELNET protocol implementation for C")

set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_GENERATE_MAN NO)
set(DOXYGEN_EXTRACT_ALL NO)
set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)

doxygen_add_docs(minitelnet_docs
    ${PROJECT_SOURCE_DIR}/minitelnet.h
    ${PROJECT_SOURCE_DIR}/minitelnet-def.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mainpage.dox
    ALL
    COMMENT "Generate minitelnet documentation"
)
