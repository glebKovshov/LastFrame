cmake_minimum_required(VERSION 3.28)

project(LastFrame
    VERSION 0.1.0
    DESCRIPTION "Local instant replay recorder"
    LANGUAGES CXX
)

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LASTFRAME_BUILD_UI "Build the Qt desktop application" ON)
option(LASTFRAME_BUILD_TESTS "Build core unit tests" ON)
option(LASTFRAME_BUILD_SMOKE "Build the real desktop capture smoke test" OFF)

find_package(nlohmann_json 3.11.3 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(lastframe_core STATIC
    src/core/AppState.cpp
    src/core/BoundedQueue.cpp
    src/core/FilenameAllocator.cpp
    src/core/RateLimiter.cpp
    src/core/Settings.cpp
    src/core/SettingsStore.cpp
    src/core/RingBuffer.cpp
)

target_include_directories(lastframe_core PUBLIC src)
target_compile_features(lastframe_core PUBLIC cxx_std_23)
target_compile_definitions(lastframe_core PUBLIC
    LASTFRAME_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    LASTFRAME_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    LASTFRAME_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
target_link_libraries(lastframe_core PUBLIC nlohmann_json::nlohmann_json)

if(WIN32)
    target_compile_definitions(lastframe_core PUBLIC NOMINMAX UNICODE _UNICODE)
endif()

if(LASTFRAME_BUILD_TESTS)
    add_executable(lastframe_core_tests
        tests/core_tests.cpp
    )
    target_link_libraries(lastframe_core_tests PRIVATE lastframe_core)
    add_test(NAME lastframe_core_tests COMMAND lastframe_core_tests)
endif()

if(LASTFRAME_BUILD_UI)
    find_package(Qt6 6.8 COMPONENTS Core Gui Widgets Network QUIET)
    if(Qt6_FOUND)
        qt_standard_project_setup()

        qt_add_executable(LastFrame
            src/app/main.cpp
            src/platform/GlobalHotkeyManager.cpp
            src/platform/AudioDeviceEnumerator.cpp
            src/platform/AudioDeviceEnumerator.h
            src/platform/MonitorEnumerator.cpp
            src/platform/Diagnostics.cpp
            src/platform/Diagnostics.h
            src/platform/FfmpegCapabilityProbe.cpp
            src/platform/FfmpegCapabilityProbe.h
            src/platform/UpdateChecker.cpp
            src/platform/UpdateChecker.h
            src/media/PortableSegmentRecorder.cpp
            src/ui/MainWindow.cpp
            src/ui/MainWindow.h
            src/ui/RegionSelector.cpp
            src/ui/RegionSelector.h
        )

        target_link_libraries(LastFrame PRIVATE
            lastframe_core
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Qt6::Network
        )

        target_include_directories(LastFrame PRIVATE src)
        target_compile_definitions(LastFrame PRIVATE LASTFRAME_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

        if(WIN32)
            target_link_libraries(LastFrame PRIVATE user32 shell32 ole32)
        endif()

        set_target_properties(LastFrame PROPERTIES
            WIN32_EXECUTABLE TRUE
            MACOSX_BUNDLE TRUE
        )

        if(LASTFRAME_BUILD_SMOKE)
            add_executable(lastframe_recorder_smoke
                tests/recorder_smoke.cpp
                src/platform/AudioDeviceEnumerator.cpp
                src/platform/MonitorEnumerator.cpp
                src/media/PortableSegmentRecorder.cpp
            )
            target_include_directories(lastframe_recorder_smoke PRIVATE src)
            target_link_libraries(lastframe_recorder_smoke PRIVATE
                lastframe_core
                Qt6::Core
                Qt6::Gui
                Qt6::Network
                Qt6::Widgets
            )
            # This test captures the real desktop and is intentionally kept
            # as a manual target, not registered in CTest/CI.

            qt_add_executable(lastframe_capability_smoke
                tests/capability_probe_smoke.cpp
                src/platform/FfmpegCapabilityProbe.cpp
                src/platform/FfmpegCapabilityProbe.h
            )
            target_include_directories(lastframe_capability_smoke PRIVATE src)
            target_link_libraries(lastframe_capability_smoke PRIVATE Qt6::Core)
        endif()
    else()
        message(WARNING "Qt 6.8+ was not found; the core and tests will still be configured, but LastFrame UI is skipped.")
    endif()
endif()

install(TARGETS lastframe_core
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

if(TARGET LastFrame)
    install(TARGETS LastFrame
        BUNDLE DESTINATION .
        RUNTIME DESTINATION .
    )
endif()
