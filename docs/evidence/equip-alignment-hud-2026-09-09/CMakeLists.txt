cmake_minimum_required(VERSION 3.24)
project(PreyVREquipAlignmentAudit LANGUAGES CXX)
add_executable(equip_alignment_audit EquipAlignmentAudit.cpp
    snapshot/src/common/AnimIk.cpp
    snapshot/src/common/StereoCamera.cpp
    snapshot/src/common/VrMath.cpp)
target_include_directories(equip_alignment_audit PRIVATE snapshot/include)
target_compile_features(equip_alignment_audit PRIVATE cxx_std_20)
