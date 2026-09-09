cmake_minimum_required(VERSION 3.24)
project(PreyVRIkCrosstalkAudit LANGUAGES CXX)
add_executable(ik_crosstalk_audit IkCrosstalkAudit.cpp
    snapshot/src/common/AnimIk.cpp
    snapshot/src/common/StereoCamera.cpp
    snapshot/src/common/VrMath.cpp)
target_include_directories(ik_crosstalk_audit PRIVATE snapshot/include)
target_compile_features(ik_crosstalk_audit PRIVATE cxx_std_20)
