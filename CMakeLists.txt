cmake_minimum_required(VERSION 3.24)

project(PreyVR LANGUAGES C CXX)

option(PREYVR_BUILD_TESTS "Build deterministic math/unit tests" ON)
option(PREYVR_BUILD_TOOLS "Build local research helpers" ON)
option(PREYVR_BUILD_SMOKE_DLL "Build the inert, signature-gated injection smoke DLL" ON)
option(PREYVR_BUILD_OPENXR_LOADER "Package the x64 OpenXR loader for preflight-only bootstrap work" ON)
option(PREYVR_REQUIRE_GAME_BASELINE "Fail tests when the researched Prey installation is unavailable" OFF)
set(PREYVR_BUILD_VERSION "0.3.0-lifecycle-hardening")
set(PREYVR_SUPPORTED_PREYDLL_SHA256 "7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7")
set(PREYVR_MINHOOK_GIT_COMMIT "c3fcafdc10146beb5919319d0683e44e3c30d537" CACHE STRING "Pinned MinHook commit (v1.3.4)")
set(PREYVR_OPENXR_GIT_COMMIT "64f2b37c8c6da3d83c9b4d11865ba1fb752cb8ec" CACHE STRING "Pinned OpenXR-SDK commit (release-1.1.60)")
set(
    PREYVR_GAME_RELEASE_DIR
    "D:/SteamLibrary/steamapps/common/Prey/Binaries/Danielle/x64/Release"
    CACHE PATH
    "Prey release directory used by the read-only build doctor"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # Deterministic output. Without /Brepro the linker writes the wall-clock link
    # time into IMAGE_FILE_HEADER.TimeDateStamp, so every relink produces a new
    # SHA-256 even from identical source -- see FAILURE_REGISTRY F-005, where a
    # documented artifact hash became unreproducible and the binary a live-test
    # procedure named was lost. /Brepro replaces the timestamp with a content
    # hash. These binaries carry no CODEVIEW debug entry, so no PDB signature is
    # embedded and the timestamp is expected to be the only varying field.
    add_link_options($<$<C_COMPILER_ID:MSVC>:/Brepro> $<$<CXX_COMPILER_ID:MSVC>:/Brepro>)
endif()

include(FetchContent)
if(PREYVR_BUILD_SMOKE_DLL AND WIN32)
    FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG ${PREYVR_MINHOOK_GIT_COMMIT}
        GIT_SHALLOW FALSE
    )
    if(PREYVR_BUILD_OPENXR_LOADER)
        set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
        set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
        set(DYNAMIC_LOADER ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            openxr
            GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
            GIT_TAG ${PREYVR_OPENXR_GIT_COMMIT}
            GIT_SHALLOW FALSE
        )
        FetchContent_MakeAvailable(minhook openxr)
    else()
        FetchContent_MakeAvailable(minhook)
    endif()
endif()

configure_file(
    cmake/Version.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h"
    @ONLY
)

# Engine-specific state remains in small typed contracts. The only current
# game-facing target is an inert bootstrap/signature smoke DLL.
add_library(preyvr_headers INTERFACE)
target_include_directories(preyvr_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(preyvr_core STATIC
    src/common/AimState.cpp
    src/common/EngineMap.cpp
    src/common/FrameObserver.cpp
    src/common/InteractionQuery.cpp
    src/common/OpenXRBootstrap.cpp
    src/common/RuntimeSnapshot.cpp
    src/common/VrMath.cpp
    src/common/WrenchQuery.cpp
)
target_link_libraries(preyvr_core PUBLIC preyvr_headers)

function(preyvr_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
preyvr_enable_warnings(preyvr_core)

if((PREYVR_BUILD_TOOLS OR PREYVR_BUILD_SMOKE_DLL) AND WIN32)
    add_executable(preyvr_engine_map_probe tools/engine_map_probe/main.cpp)
    target_compile_definitions(preyvr_engine_map_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(preyvr_engine_map_probe PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_engine_map_probe)
endif()

if(PREYVR_BUILD_SMOKE_DLL AND WIN32)
    add_library(preyvr_win32_bootstrap STATIC
        src/dll/OpenXRPreflightWin32.cpp
        src/dll/RuntimeSnapshotWin32.cpp
    )
    target_include_directories(preyvr_win32_bootstrap PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/dll")
    target_compile_definitions(preyvr_win32_bootstrap PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(preyvr_win32_bootstrap PUBLIC preyvr_core advapi32)
    preyvr_enable_warnings(preyvr_win32_bootstrap)

    add_library(preyvr SHARED
        src/dll/Bootstrap.cpp
        src/dll/DllMain.cpp
        src/dll/FrameObserverHook.cpp
        src/lifecycle/Logger.cpp
    )
    target_include_directories(preyvr PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lifecycle"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_compile_definitions(preyvr PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(preyvr PRIVATE preyvr_core preyvr_win32_bootstrap minhook bcrypt psapi shell32)
    preyvr_enable_warnings(preyvr)
    set_target_properties(preyvr PROPERTIES OUTPUT_NAME PreyVR)
    if(TARGET preyvr_engine_map_probe)
        add_dependencies(preyvr preyvr_engine_map_probe)
    endif()
    if(PREYVR_BUILD_OPENXR_LOADER)
        add_dependencies(preyvr openxr_loader)
        add_custom_command(
            TARGET preyvr
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:openxr_loader>
                $<TARGET_FILE_DIR:preyvr>/$<TARGET_FILE_NAME:openxr_loader>
            COMMAND
                ${CMAKE_COMMAND}
                -DPREYVR_DLL=$<TARGET_FILE:preyvr>
                -DPREYVR_OPENXR_LOADER=$<TARGET_FILE_DIR:preyvr>/$<TARGET_FILE_NAME:openxr_loader>
                -DPREYVR_MANIFEST=$<TARGET_FILE_DIR:preyvr>/PreyVR-build-manifest.txt
                -DPREYVR_VERSION=${PREYVR_BUILD_VERSION}
                -DPREYVR_CONFIGURATION=$<CONFIG>
                -DPREYVR_EXPECTED_PREYDLL_SHA256=${PREYVR_SUPPORTED_PREYDLL_SHA256}
                -DPREYVR_ENGINE_MAP_PROBE=$<TARGET_FILE:preyvr_engine_map_probe>
                -DPREYVR_MINHOOK_COMMIT=${PREYVR_MINHOOK_GIT_COMMIT}
                -DPREYVR_OPENXR_COMMIT=${PREYVR_OPENXR_GIT_COMMIT}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteBuildManifest.cmake"
            VERBATIM
        )
    else()
        add_custom_command(
            TARGET preyvr
            POST_BUILD
            COMMAND
                ${CMAKE_COMMAND}
                -DPREYVR_DLL=$<TARGET_FILE:preyvr>
                -DPREYVR_MANIFEST=$<TARGET_FILE_DIR:preyvr>/PreyVR-build-manifest.txt
                -DPREYVR_VERSION=${PREYVR_BUILD_VERSION}
                -DPREYVR_CONFIGURATION=$<CONFIG>
                -DPREYVR_EXPECTED_PREYDLL_SHA256=${PREYVR_SUPPORTED_PREYDLL_SHA256}
                -DPREYVR_ENGINE_MAP_PROBE=$<TARGET_FILE:preyvr_engine_map_probe>
                -DPREYVR_MINHOOK_COMMIT=${PREYVR_MINHOOK_GIT_COMMIT}
                -DPREYVR_OPENXR_COMMIT=${PREYVR_OPENXR_GIT_COMMIT}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteBuildManifest.cmake"
            VERBATIM
        )
    endif()
endif()

if(PREYVR_BUILD_TOOLS AND WIN32)
    add_executable(preyvr_dxgi_probe tools/dxgi_probe/main.cpp)
    target_link_libraries(preyvr_dxgi_probe PRIVATE d3d11 dxgi user32)
    preyvr_enable_warnings(preyvr_dxgi_probe)

    add_executable(preyvr_ray_probe tools/ray_probe/main.cpp)
    target_link_libraries(preyvr_ray_probe PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_ray_probe)

    # Answers the adapter-LUID question out of process, so it needs no running
    # Prey and no headset to build. Skipped when the loader is not being built,
    # since without it there is nothing to ask.
    if(PREYVR_BUILD_OPENXR_LOADER AND TARGET openxr_loader)
        add_executable(preyvr_xr_adapter_probe tools/xr_adapter_probe/main.cpp)
        target_compile_definitions(preyvr_xr_adapter_probe PRIVATE
            WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11)
        target_link_libraries(preyvr_xr_adapter_probe PRIVATE openxr_loader d3d11 dxgi)
        preyvr_enable_warnings(preyvr_xr_adapter_probe)
        add_custom_command(
            TARGET preyvr_xr_adapter_probe
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:openxr_loader>
                $<TARGET_FILE_DIR:preyvr_xr_adapter_probe>/$<TARGET_FILE_NAME:openxr_loader>
            VERBATIM
        )
    endif()
endif()

if(PREYVR_BUILD_TESTS)
    enable_testing()

    add_executable(preyvr_vr_math_tests tests/VrMathTests.cpp)
    target_link_libraries(preyvr_vr_math_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_vr_math_tests)
    add_test(NAME vr_math COMMAND preyvr_vr_math_tests)

    add_executable(preyvr_engine_map_tests tests/EngineMapTests.cpp)
    target_link_libraries(preyvr_engine_map_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_engine_map_tests)
    add_test(NAME engine_map COMMAND preyvr_engine_map_tests)

    add_executable(preyvr_aim_state_tests tests/AimStateTests.cpp)
    target_link_libraries(preyvr_aim_state_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_aim_state_tests)
    add_test(NAME aim_state_fixture COMMAND preyvr_aim_state_tests)

    add_executable(preyvr_wrench_query_tests tests/WrenchQueryTests.cpp)
    target_link_libraries(preyvr_wrench_query_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_wrench_query_tests)
    add_test(NAME wrench_query_fixture COMMAND preyvr_wrench_query_tests)

    add_executable(preyvr_interaction_query_tests tests/InteractionQueryTests.cpp)
    target_link_libraries(preyvr_interaction_query_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_interaction_query_tests)
    add_test(NAME interaction_query_fixture COMMAND preyvr_interaction_query_tests)

    add_executable(preyvr_runtime_snapshot_tests tests/RuntimeSnapshotTests.cpp)
    target_link_libraries(preyvr_runtime_snapshot_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_runtime_snapshot_tests)
    add_test(NAME runtime_snapshot COMMAND preyvr_runtime_snapshot_tests)

    add_executable(preyvr_runtime_bootstrap_tests tests/RuntimeBootstrapTests.cpp)
    target_link_libraries(preyvr_runtime_bootstrap_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_runtime_bootstrap_tests)
    add_test(NAME runtime_bootstrap COMMAND preyvr_runtime_bootstrap_tests)

    if(PREYVR_BUILD_SMOKE_DLL AND WIN32)
        add_executable(preyvr_dll_smoke_host tests/DllSmokeHost.cpp)
        preyvr_enable_warnings(preyvr_dll_smoke_host)
        add_test(NAME dll_load_fail_closed COMMAND preyvr_dll_smoke_host $<TARGET_FILE:preyvr>)
        set_tests_properties(
            dll_load_fail_closed
            PROPERTIES
                ENVIRONMENT "PREYVR_LOG_PATH=${CMAKE_CURRENT_BINARY_DIR}/Testing/Temporary/preyvr-smoke-host.log"
                PASS_REGULAR_EXPRESSION "dll_smoke_host status=unsupported_as_expected"
        )

        if(PREYVR_BUILD_OPENXR_LOADER)
            add_executable(preyvr_openxr_preflight_tests tests/OpenXRPreflightWin32Tests.cpp)
            target_include_directories(preyvr_openxr_preflight_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/dll")
            target_link_libraries(preyvr_openxr_preflight_tests PRIVATE preyvr_win32_bootstrap)
            preyvr_enable_warnings(preyvr_openxr_preflight_tests)
            add_test(
                NAME openxr_preflight_files
                COMMAND preyvr_openxr_preflight_tests $<TARGET_FILE:openxr_loader> $<TARGET_FILE:preyvr>
            )
        endif()
    endif()

    if(PREYVR_BUILD_TOOLS AND WIN32)
        add_test(NAME dxgi_method_probe COMMAND preyvr_dxgi_probe)
        set_tests_properties(
            dxgi_method_probe
            PROPERTIES PASS_REGULAR_EXPRESSION "IDXGISwapChain::Present.*dxgi_rva="
        )
    endif()

    find_program(PREYVR_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(PREYVR_POWERSHELL_EXECUTABLE AND PREYVR_BUILD_SMOKE_DLL AND WIN32)
        add_test(
            NAME smoke_log_parser
            COMMAND
                "${PREYVR_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/Test-PreyVRSmokeLog.ps1"
                -LogPath "${CMAKE_CURRENT_BINARY_DIR}/Testing/Temporary/preyvr-smoke-host.log"
                -ExpectedStatus unsupported
                -ExpectedLandmarkCount 0
        )
        set_tests_properties(
            smoke_log_parser
            PROPERTIES
                DEPENDS dll_load_fail_closed
                PASS_REGULAR_EXPRESSION "smoke_log_check result=pass"
        )
    endif()
    if(PREYVR_POWERSHELL_EXECUTABLE AND PREYVR_BUILD_TOOLS AND WIN32)
        set(PREYVR_DOCTOR_ARGUMENTS
            -GameReleasePath "${PREYVR_GAME_RELEASE_DIR}"
            -EngineMapProbe $<TARGET_FILE:preyvr_engine_map_probe>
        )
        if(NOT PREYVR_REQUIRE_GAME_BASELINE)
            list(APPEND PREYVR_DOCTOR_ARGUMENTS -AllowMissingGame)
        endif()
        add_test(
            NAME build_doctor
            COMMAND
                "${PREYVR_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/Invoke-PreyVRDoctor.ps1"
                ${PREYVR_DOCTOR_ARGUMENTS}
        )
        set_tests_properties(
            build_doctor
            PROPERTIES PASS_REGULAR_EXPRESSION "SUMMARY: pass=.*fail=0"
        )
    endif()
endif()
