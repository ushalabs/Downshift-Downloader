cmake_minimum_required(VERSION 3.25)
project(Downshift VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Downshift requires Windows and the MSVC C++ toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Downshift requires a 64-bit build.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
find_package(CURL REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_library(downshift_foundation STATIC
    src/core/Runtime.cpp
    src/core/Runtime.h
)
target_include_directories(downshift_foundation PUBLIC src)
target_link_libraries(downshift_foundation PUBLIC
    CURL::libcurl unofficial::sqlite3::sqlite3
)
target_compile_options(downshift_foundation PRIVATE /W4 /permissive- /utf-8)

qt_add_executable(downshift WIN32
    src/main.cpp
    src/gui/MainWindow.cpp
    src/gui/MainWindow.h
)
set_target_properties(downshift PROPERTIES OUTPUT_NAME Downshift)
target_link_libraries(downshift PRIVATE downshift_foundation Qt6::Widgets)
target_compile_definitions(downshift PRIVATE
    DOWNSHIFT_VERSION="${PROJECT_VERSION}"
)
target_compile_options(downshift PRIVATE /W4 /permissive- /utf-8)

# vcpkg copies the linked DLLs. Copy Qt's platform plugins as well so the
# development executable can start without a system-wide Qt installation.
add_custom_command(TARGET downshift POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:downshift>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
        "$<TARGET_FILE_DIR:downshift>/platforms/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
        "$<TARGET_FILE_DIR:downshift>/platforms/"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    add_executable(downshift_foundation_tests tests/FoundationTests.cpp)
    target_link_libraries(downshift_foundation_tests PRIVATE downshift_foundation)
    target_compile_options(downshift_foundation_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME foundation.dependencies COMMAND downshift_foundation_tests)
    add_test(NAME foundation.gui.offscreen COMMAND downshift --smoke-test -platform offscreen)
    add_test(NAME foundation.gui.windows COMMAND downshift --smoke-test)
    set_tests_properties(foundation.dependencies PROPERTIES TIMEOUT 15)
    set_tests_properties(foundation.gui.offscreen foundation.gui.windows PROPERTIES TIMEOUT 20)
endif()
