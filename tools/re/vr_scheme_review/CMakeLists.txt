cmake_minimum_required(VERSION 3.20)
project(preyvr_scheme_review LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_executable(scheme_review review.cpp
    "${REPO}/src/common/Locomotion.cpp"
    "${REPO}/src/common/XrFrameContract.cpp")
target_compile_features(scheme_review PRIVATE cxx_std_20)
target_compile_definitions(scheme_review PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(scheme_review PRIVATE
    "${REPO}/include" "${REPO}/src/lifecycle"
    "${REPO}/build/headless/_deps/minhook-src/include")

add_executable(aim_review aim_review.cpp)
target_compile_features(aim_review PRIVATE cxx_std_20)
target_compile_definitions(aim_review PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(aim_review PRIVATE
    "${REPO}/include" "${REPO}/src/lifecycle"
    "${REPO}/build/headless/_deps/minhook-src/include")
target_sources(aim_review PRIVATE
    "${REPO}/src/common/VrMath.cpp" "${REPO}/src/common/AnimIk.cpp"
    "${REPO}/src/common/EngineMap.cpp" "${REPO}/src/common/StereoCamera.cpp"
    "${REPO}/src/common/MotionController.cpp" "${REPO}/src/common/WeaponAim.cpp")

add_executable(hud_queue_review hud_queue_review.cpp)
target_compile_features(hud_queue_review PRIVATE cxx_std_20)
target_compile_definitions(hud_queue_review PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(hud_queue_review PRIVATE "${REPO}/include" "${REPO}/src/lifecycle")
enable_testing()
add_test(NAME scheme_review COMMAND scheme_review)
add_test(NAME aim_review COMMAND aim_review)
add_test(NAME hud_queue_review COMMAND hud_queue_review)
