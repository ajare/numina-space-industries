# The headless Editor document/session seam.
#
# This library holds the Space Station -> Deck -> Cell model and the session
# commands the Editor drives. It links no graphics or native dialogs.
# The real YAML storage adapter is exercised through the same session seam
# by the GUI and consumer tests without a desktop or GL context.

add_library(editor-document STATIC
    src/Deck.cpp
    src/SpaceStation.cpp
    src/EditorSession.cpp
    src/DocumentIO.cpp
)
add_library(editor::document ALIAS editor-document)
target_link_libraries(editor-document PUBLIC Promethium::Serialization)
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
