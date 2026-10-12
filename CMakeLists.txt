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

add_library(downshift_engine STATIC
    src/core/DownloadEngine/DownloadEngine.cpp
    src/core/DownloadEngine/HttpResponse.cpp
    src/core/FileWriter/FileWriter.cpp
    src/core/ResumeManager/ResumeJournal.cpp
)
target_link_libraries(downshift_engine PUBLIC downshift_foundation PRIVATE bcrypt crypt32)
target_compile_options(downshift_engine PRIVATE /W4 /permissive- /utf-8)

add_executable(downshift_download src/DownloadCli.cpp)
set_target_properties(downshift_download PROPERTIES OUTPUT_NAME DownshiftDownload)
target_link_libraries(downshift_download PRIVATE downshift_engine)
target_compile_options(downshift_download PRIVATE /W4 /permissive- /utf-8)

add_library(downshift_queue STATIC
    src/database/QueueStore.cpp
    src/core/DownloadQueue/QueueManager.cpp
)
target_link_libraries(downshift_queue PUBLIC downshift_engine Qt6::Core PRIVATE crypt32)
target_compile_options(downshift_queue PRIVATE /W4 /permissive- /utf-8)

add_library(downshift_browser STATIC src/browser/NativeProtocol.cpp src/browser/PipeServer.cpp)
target_link_libraries(downshift_browser PUBLIC downshift_queue PRIVATE advapi32)
target_compile_options(downshift_browser PRIVATE /W4 /permissive- /utf-8)
add_executable(downshift_native_host src/browser/NativeHost.cpp)
set_target_properties(downshift_native_host PROPERTIES OUTPUT_NAME DownshiftNativeHost)
target_link_libraries(downshift_native_host PRIVATE downshift_browser)
target_compile_options(downshift_native_host PRIVATE /W4 /permissive- /utf-8)

add_library(downshift_gui STATIC
    src/gui/MainWindow.cpp
    src/gui/MainWindow.h
    src/gui/Dialogs.cpp
)
target_link_libraries(downshift_gui PUBLIC downshift_queue Qt6::Widgets)
target_compile_options(downshift_gui PRIVATE /W4 /permissive- /utf-8)
qt_add_executable(downshift WIN32 src/main.cpp)
set_target_properties(downshift PROPERTIES OUTPUT_NAME Downshift)
target_link_libraries(downshift PRIVATE downshift_gui downshift_browser)
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
        "$<TARGET_FILE:Qt6::QMinimalIntegrationPlugin>"
        "$<TARGET_FILE_DIR:downshift>/platforms/"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    find_program(NODE_EXECUTABLE NAMES node)
    if(NODE_EXECUTABLE)
        add_test(NAME browser.extension COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/ExtensionTests.mjs")
        set_tests_properties(browser.extension PROPERTIES TIMEOUT 30)
    else()
        message(STATUS "Node.js unavailable: extension state checks require a separate Node test run.")
    endif()
    add_executable(downshift_browser_tests tests/BrowserDriver.cpp)
    target_link_libraries(downshift_browser_tests PRIVATE downshift_browser downshift_gui)
    target_compile_options(downshift_browser_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME browser.native COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/BrowserTests.py" "$<TARGET_FILE:downshift_native_host>" "$<TARGET_FILE:downshift_browser_tests>")
    set_tests_properties(browser.native PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_executable(downshift_gui_tests tests/GuiTests.cpp)
    target_link_libraries(downshift_gui_tests PRIVATE downshift_gui)
    target_compile_options(downshift_gui_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME gui.integration COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/GuiIntegration.py" "$<TARGET_FILE:downshift_gui_tests>"
        "${CMAKE_CURRENT_BINARY_DIR}/gui-shots/$<CONFIG>")
    set_tests_properties(gui.integration PROPERTIES TIMEOUT 90)
    add_executable(downshift_queue_tests tests/QueueDriver.cpp)
    target_link_libraries(downshift_queue_tests PRIVATE downshift_queue)
    target_compile_options(downshift_queue_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME queue.integration COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/QueueTests.py" "$<TARGET_FILE:downshift_queue_tests>")
    set_tests_properties(queue.integration PROPERTIES TIMEOUT 120)
    add_executable(downshift_engine_tests tests/EngineTests.cpp)
    target_link_libraries(downshift_engine_tests PRIVATE downshift_engine)
    target_compile_options(downshift_engine_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME engine.unit COMMAND downshift_engine_tests)
    add_test(NAME engine.integration COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/IntegrationTests.py"
        "$<TARGET_FILE:downshift_download>" "$<TARGET_FILE:downshift_engine_tests>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    set_tests_properties(engine.unit PROPERTIES TIMEOUT 30)
    set_tests_properties(engine.integration PROPERTIES TIMEOUT 180)
    add_test(NAME engine.adaptive COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/PerformanceTests.py"
        "$<TARGET_FILE:downshift_download>" "$<TARGET_FILE:downshift_engine_tests>")
    set_tests_properties(engine.adaptive PROPERTIES TIMEOUT 150)
    add_test(NAME engine.buffered COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/BufferedTests.py" "$<TARGET_FILE:downshift_download>")
    set_tests_properties(engine.buffered PROPERTIES TIMEOUT 90)
    add_test(NAME engine.recovery COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/RecoveryTests.py"
        "$<TARGET_FILE:downshift_download>" "$<TARGET_FILE:downshift_engine_tests>")
    set_tests_properties(engine.recovery PROPERTIES TIMEOUT 180)
    add_executable(downshift_foundation_tests tests/FoundationTests.cpp)
    target_link_libraries(downshift_foundation_tests PRIVATE downshift_foundation)
    target_compile_options(downshift_foundation_tests PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME foundation.dependencies COMMAND downshift_foundation_tests)
    add_test(NAME foundation.gui.minimal COMMAND downshift --smoke-test -platform minimal)
    add_test(NAME foundation.gui.windows COMMAND downshift --smoke-test)
    set_tests_properties(foundation.dependencies PROPERTIES TIMEOUT 15)
    set_tests_properties(foundation.gui.minimal foundation.gui.windows PROPERTIES TIMEOUT 20)
endif()
