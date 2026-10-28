# The headless Editor document/session seam.
#
# This library drives the core Installation through headless session commands.
# It links no graphics or native dialogs.
# The real YAML storage adapter is exercised through the same session seam
# by the GUI and consumer tests without a desktop or GL context.

add_library(editor-document STATIC
    src/EditorSession.cpp
    src/DocumentIO.cpp
)
add_library(editor::document ALIAS editor-document)
target_link_libraries(editor-document PUBLIC nsi::core)
target_compile_features(editor-document PUBLIC cxx_std_20)
target_include_directories(editor-document PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
set_target_properties(editor-document PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(editor-document PRIVATE /W3 /permissive-)
else()
    target_compile_options(editor-document PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(editor-atmosphere-identity tests/AtmosphereIdentity.cpp)
    target_link_libraries(editor-atmosphere-identity PRIVATE editor::document nsi::systemsBridge)
    add_test(NAME editor-atmosphere-identity COMMAND editor-atmosphere-identity)
    set_tests_properties(editor-atmosphere-identity PROPERTIES
        LABELS "editor;systems;functional;validation-fast" TIMEOUT 30)

    add_executable(editor-installation-editing tests/InstallationEditing.cpp)
    target_link_libraries(editor-installation-editing PRIVATE editor::document)
    add_test(NAME editor-installation-editing COMMAND editor-installation-editing)
    set_tests_properties(editor-installation-editing PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    # Consumer-style: public headers only, runtime checks that survive Release.
    add_executable(editor-document-consumer tests/DocumentConsumer.cpp)
    target_compile_features(editor-document-consumer PRIVATE cxx_std_20)
    target_link_libraries(editor-document-consumer PRIVATE editor::document)
    add_test(NAME editor-document-session COMMAND editor-document-consumer)
    set_tests_properties(editor-document-session PROPERTIES
        LABELS "editor;functional;validation-fast" TIMEOUT 30)

    add_executable(editor-persistence-consumer tests/PersistenceConsumer.cpp)
    target_link_libraries(editor-persistence-consumer PRIVATE editor::document)
    add_test(NAME editor-document-persistence COMMAND editor-persistence-consumer)
    set_tests_properties(editor-document-persistence PROPERTIES
        LABELS "editor;persistence;functional;validation-fast" TIMEOUT 30)
endif()
