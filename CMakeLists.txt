cmake_minimum_required(VERSION 3.24)

project(PreyVR LANGUAGES C CXX)

option(PREYVR_BUILD_TESTS "Build deterministic math/unit tests" ON)
option(PREYVR_BUILD_TOOLS "Build local research helpers" ON)
option(PREYVR_BUILD_SMOKE_DLL "Build the inert, signature-gated injection smoke DLL" ON)
option(PREYVR_BUILD_OPENXR_LOADER "Package the x64 OpenXR loader for preflight-only bootstrap work" ON)
option(PREYVR_REQUIRE_GAME_BASELINE "Fail tests when the researched Prey installation is unavailable" OFF)
set(PREYVR_BUILD_VERSION "0.3.1-static-ik-integration")
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
    src/common/AnimIk.cpp
    src/common/WeaponRigAlignment.cpp
    src/common/CameraEdit.cpp
    src/common/ConsolePolicy.cpp
    src/common/DebugRegisters.cpp
    src/common/EngineMap.cpp
    src/common/FrameDump.cpp
    src/common/FrameObserver.cpp
    src/common/InputEvent.cpp
    src/common/InputQueue.cpp
    src/common/InteractionQuery.cpp
    src/common/Locomotion.cpp
    src/common/MenuNavigator.cpp
    src/common/InventoryDepth.cpp
    src/common/UiPanel.cpp
    src/common/MotionController.cpp
    src/common/OpenXRBootstrap.cpp
    src/common/RenderFrameTable.cpp
    src/common/RuntimeSnapshot.cpp
    src/common/StereoCamera.cpp
    src/common/StereoFrame.cpp
    src/common/VrMath.cpp
    src/common/DepthSubmission.cpp
    src/common/FrameClock.cpp
    src/common/FrameTiming.cpp
    src/common/FrustumCoverage.cpp
    src/common/WeaponAim.cpp
    src/common/WrenchQuery.cpp
    src/common/XrFrameContract.cpp
    src/common/XrSwapchainFormat.cpp
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
    add_executable(preyvr_injector tools/injector/main.cpp)
    target_compile_definitions(preyvr_injector PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    preyvr_enable_warnings(preyvr_injector)
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
        src/dll/AimRayProbe.cpp
        src/dll/AimTakeover.cpp
        src/dll/AnimIkTakeover.cpp
        src/dll/Bootstrap.cpp
        src/dll/VrMode.cpp
        src/dll/UiGuide.cpp
        src/dll/HudLayer.cpp
        src/dll/UiPointer.cpp
        src/dll/InventoryPointerCapture.cpp
        src/dll/CameraEditHook.cpp
        src/dll/ConsoleBridgeWin32.cpp
        src/dll/DebugWatch.cpp
        src/dll/DllMain.cpp
        src/dll/FrameCaptureWin32.cpp
        src/dll/FrameObserverHook.cpp
        src/dll/HeadTrackingHook.cpp
        src/dll/HudBridge.cpp
        src/dll/HotkeyBridge.cpp
        src/dll/IkTargetProbe.cpp
        src/dll/InputPathProbe.cpp
        src/dll/InputPost.cpp
        src/dll/MinHookInit.cpp
        src/dll/MoveLane.cpp
        src/dll/CommandChannel.cpp
        src/dll/HandRigTakeover.cpp
        src/dll/NearFovOverride.cpp
        src/dll/NearViewStereo.cpp
        src/dll/RenderFrame.cpp
        src/dll/WeaponAttachment.cpp
        src/dll/PassCameraProbe.cpp
        src/dll/ReticleFollow.cpp
        src/dll/XrInput.cpp
        src/dll/XrSessionHost.cpp
        src/lifecycle/Logger.cpp
    )
    target_include_directories(preyvr PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lifecycle"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_compile_definitions(preyvr PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(preyvr PRIVATE preyvr_core preyvr_win32_bootstrap minhook bcrypt psapi shell32 d3d11 dxgi gdi32)
    if(PREYVR_BUILD_OPENXR_LOADER AND TARGET openxr_loader)
        # The in-process session host needs the loader. The DLL already ships
        # openxr_loader.dll beside itself for the preflight, so this adds a link
        # dependency rather than a new deployment requirement.
        target_link_libraries(preyvr PRIVATE openxr_loader delayimp)
        target_compile_definitions(preyvr PRIVATE XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11)
        # **Delay-loaded on purpose.** A hard import would make PreyVR.dll fail to
        # load at all when openxr_loader.dll is not beside it -- the landmark gate
        # would never run, nothing would reach the log, and the failure would read
        # as "the DLL is broken" rather than "a dependency is missing". Injection
        # is precisely where an adjacent DLL may not be on the search path.
        # Delay-loading keeps the fail-closed lifecycle intact and turns a missing
        # loader into a clean refusal inside StartXrSession instead.
        target_link_options(preyvr PRIVATE /DELAYLOAD:openxr_loader.dll)
    endif()
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

    add_executable(preyvr_frame_diff tools/frame_diff/main.cpp)
    target_link_libraries(preyvr_frame_diff PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_frame_diff)

    # Answers the adapter-LUID question out of process, so it needs no running
    # Prey and no headset to build. Skipped when the loader is not being built,
    # since without it there is nothing to ask.
    if(PREYVR_BUILD_OPENXR_LOADER AND TARGET openxr_loader)
        # The full session lifecycle, runnable against xr-sim without a headset.
        add_executable(preyvr_xr_session_probe tools/xr_session_probe/main.cpp)
        target_compile_definitions(preyvr_xr_session_probe PRIVATE
            WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11)
        target_link_libraries(preyvr_xr_session_probe PRIVATE preyvr_core openxr_loader d3d11 dxgi)
        preyvr_enable_warnings(preyvr_xr_session_probe)
        add_custom_command(
            TARGET preyvr_xr_session_probe
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:openxr_loader>
                $<TARGET_FILE_DIR:preyvr_xr_session_probe>/$<TARGET_FILE_NAME:openxr_loader>
            VERBATIM
        )

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

    add_executable(preyvr_xr_swapchain_format_tests tests/XrSwapchainFormatTests.cpp)
    target_link_libraries(preyvr_xr_swapchain_format_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_xr_swapchain_format_tests)
    add_test(NAME xr_swapchain_format COMMAND preyvr_xr_swapchain_format_tests)

    add_executable(preyvr_xr_frame_contract_tests tests/XrFrameContractTests.cpp)
    target_link_libraries(preyvr_xr_frame_contract_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_xr_frame_contract_tests)
    add_test(NAME xr_frame_contract COMMAND preyvr_xr_frame_contract_tests)

    add_executable(preyvr_stereo_frame_tests tests/StereoFrameTests.cpp)
    target_link_libraries(preyvr_stereo_frame_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_stereo_frame_tests)
    add_test(NAME stereo_frame COMMAND preyvr_stereo_frame_tests)

    add_executable(preyvr_motion_controller_tests tests/MotionControllerTests.cpp)
    target_link_libraries(preyvr_motion_controller_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_motion_controller_tests)
    add_test(NAME motion_controller COMMAND preyvr_motion_controller_tests)

    add_executable(preyvr_anim_ik_tests tests/AnimIkTests.cpp)
    target_link_libraries(preyvr_anim_ik_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_anim_ik_tests)
    add_test(NAME anim_ik COMMAND preyvr_anim_ik_tests)

    add_executable(preyvr_weapon_rig_alignment_tests tests/WeaponRigAlignmentTests.cpp)
    target_link_libraries(preyvr_weapon_rig_alignment_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_weapon_rig_alignment_tests)
    add_test(NAME weapon_rig_alignment COMMAND preyvr_weapon_rig_alignment_tests)

    add_executable(preyvr_weapon_aim_tests tests/WeaponAimTests.cpp)
    target_link_libraries(preyvr_weapon_aim_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_weapon_aim_tests)
    add_test(NAME weapon_aim COMMAND preyvr_weapon_aim_tests)

    add_executable(preyvr_frustum_coverage_tests tests/FrustumCoverageTests.cpp)
    target_link_libraries(preyvr_frustum_coverage_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_frustum_coverage_tests)
    add_test(NAME frustum_coverage COMMAND preyvr_frustum_coverage_tests)

    add_executable(preyvr_depth_submission_tests tests/DepthSubmissionTests.cpp)
    target_link_libraries(preyvr_depth_submission_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_depth_submission_tests)
    add_test(NAME depth_submission COMMAND preyvr_depth_submission_tests)

    add_executable(preyvr_frame_clock_tests tests/FrameClockTests.cpp)
    target_link_libraries(preyvr_frame_clock_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_frame_clock_tests)
    add_test(NAME frame_clock COMMAND preyvr_frame_clock_tests)

    add_executable(preyvr_frame_timing_tests tests/FrameTimingTests.cpp)
    target_link_libraries(preyvr_frame_timing_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_frame_timing_tests)
    add_test(NAME frame_timing COMMAND preyvr_frame_timing_tests)

    add_executable(preyvr_input_event_tests tests/InputEventTests.cpp)
    target_link_libraries(preyvr_input_event_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_input_event_tests)
    add_test(NAME input_event COMMAND preyvr_input_event_tests)

    add_executable(preyvr_input_queue_tests tests/InputQueueTests.cpp)
    target_link_libraries(preyvr_input_queue_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_input_queue_tests)
    add_test(NAME input_queue COMMAND preyvr_input_queue_tests)

    add_executable(preyvr_menu_navigator_tests tests/MenuNavigatorTests.cpp)
    target_link_libraries(preyvr_menu_navigator_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_menu_navigator_tests)
    add_test(NAME menu_navigator COMMAND preyvr_menu_navigator_tests)
    add_executable(preyvr_inventory_depth_tests tests/InventoryDepthTests.cpp)
    target_link_libraries(preyvr_inventory_depth_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_inventory_depth_tests)
    add_test(NAME inventory_depth COMMAND preyvr_inventory_depth_tests)

    add_executable(preyvr_ui_panel_tests tests/UiPanelTests.cpp)
    target_link_libraries(preyvr_ui_panel_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_ui_panel_tests)
    add_test(NAME ui_panel COMMAND preyvr_ui_panel_tests)

    add_executable(preyvr_locomotion_tests tests/LocomotionTests.cpp)
    target_link_libraries(preyvr_locomotion_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_locomotion_tests)
    add_test(NAME locomotion COMMAND preyvr_locomotion_tests)

    add_executable(preyvr_render_frame_table_tests tests/RenderFrameTableTests.cpp)
    target_link_libraries(preyvr_render_frame_table_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_render_frame_table_tests)
    add_test(NAME render_frame_table COMMAND preyvr_render_frame_table_tests)

    add_executable(preyvr_stereo_camera_tests tests/StereoCameraTests.cpp)
    target_link_libraries(preyvr_stereo_camera_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_stereo_camera_tests)
    add_test(NAME stereo_camera COMMAND preyvr_stereo_camera_tests)

    add_executable(preyvr_camera_edit_tests tests/CameraEditTests.cpp)
    target_link_libraries(preyvr_camera_edit_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_camera_edit_tests)
    add_test(NAME camera_edit COMMAND preyvr_camera_edit_tests)

    add_executable(preyvr_frame_dump_tests tests/FrameDumpTests.cpp)
    target_link_libraries(preyvr_frame_dump_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_frame_dump_tests)
    add_test(NAME frame_dump COMMAND preyvr_frame_dump_tests)

    add_executable(preyvr_debug_registers_tests tests/DebugRegistersTests.cpp)
    target_link_libraries(preyvr_debug_registers_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_debug_registers_tests)
    add_test(NAME debug_registers COMMAND preyvr_debug_registers_tests)

    add_executable(preyvr_console_policy_tests tests/ConsolePolicyTests.cpp)
    target_link_libraries(preyvr_console_policy_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_console_policy_tests)
    add_test(NAME console_policy COMMAND preyvr_console_policy_tests)

    add_executable(preyvr_runtime_bootstrap_tests tests/RuntimeBootstrapTests.cpp)
    target_link_libraries(preyvr_runtime_bootstrap_tests PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_runtime_bootstrap_tests)
    add_test(NAME runtime_bootstrap COMMAND preyvr_runtime_bootstrap_tests)

    if(PREYVR_BUILD_SMOKE_DLL AND WIN32)
        add_executable(preyvr_ui_pointer_tests tests/UiPointerTests.cpp)
        target_include_directories(preyvr_ui_pointer_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/lifecycle")
        target_compile_definitions(preyvr_ui_pointer_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(preyvr_ui_pointer_tests PRIVATE preyvr_core)
        preyvr_enable_warnings(preyvr_ui_pointer_tests)
        add_test(NAME ui_pointer_dispatch COMMAND preyvr_ui_pointer_tests)

        add_executable(preyvr_inventory_capture_tests tests/InventoryPointerCaptureTests.cpp)
        target_include_directories(preyvr_inventory_capture_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/lifecycle")
        target_link_libraries(preyvr_inventory_capture_tests PRIVATE minhook)
        preyvr_enable_warnings(preyvr_inventory_capture_tests)
        add_test(NAME inventory_pointer_capture COMMAND preyvr_inventory_capture_tests)

        add_executable(preyvr_near_fov_tests tests/NearFovOverrideTests.cpp)
        target_include_directories(preyvr_near_fov_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/lifecycle")
        target_link_libraries(preyvr_near_fov_tests PRIVATE minhook)
        preyvr_enable_warnings(preyvr_near_fov_tests)
        add_test(NAME near_fov_override COMMAND preyvr_near_fov_tests)

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
