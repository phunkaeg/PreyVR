if(NOT DEFINED PREYVR_DLL OR NOT EXISTS "${PREYVR_DLL}")
    message(FATAL_ERROR "PREYVR_DLL is missing or does not exist")
endif()
if(NOT DEFINED PREYVR_MANIFEST)
    message(FATAL_ERROR "PREYVR_MANIFEST is required")
endif()
if(NOT DEFINED PREYVR_ENGINE_MAP_PROBE OR NOT EXISTS "${PREYVR_ENGINE_MAP_PROBE}")
    message(FATAL_ERROR "PREYVR_ENGINE_MAP_PROBE is missing or does not exist")
endif()

execute_process(
    COMMAND "${PREYVR_ENGINE_MAP_PROBE}" --count
    RESULT_VARIABLE PREYVR_LANDMARK_COUNT_RESULT
    OUTPUT_VARIABLE PREYVR_LANDMARK_COUNT
    ERROR_VARIABLE PREYVR_LANDMARK_COUNT_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT PREYVR_LANDMARK_COUNT_RESULT EQUAL 0 OR NOT PREYVR_LANDMARK_COUNT MATCHES "^[0-9]+$")
    message(FATAL_ERROR "Could not obtain landmark count: ${PREYVR_LANDMARK_COUNT_ERROR}")
endif()

file(SHA256 "${PREYVR_DLL}" PREYVR_DLL_SHA256)
set(PREYVR_OPENXR_LOADER_SHA256 "not_packaged")
if(DEFINED PREYVR_OPENXR_LOADER AND EXISTS "${PREYVR_OPENXR_LOADER}")
    file(SHA256 "${PREYVR_OPENXR_LOADER}" PREYVR_OPENXR_LOADER_SHA256)
endif()
file(WRITE "${PREYVR_MANIFEST}"
    "schema=2\n"
    "version=${PREYVR_VERSION}\n"
    "configuration=${PREYVR_CONFIGURATION}\n"
    "architecture=x64\n"
    "dll_sha256=${PREYVR_DLL_SHA256}\n"
    "supported_preydll_sha256=${PREYVR_EXPECTED_PREYDLL_SHA256}\n"
    "runtime_landmarks=${PREYVR_LANDMARK_COUNT}\n"
    "hooks=frame_observer_compiled_default_off\n"
    "openxr=preflight_only\n"
    "openxr_loader_sha256=${PREYVR_OPENXR_LOADER_SHA256}\n"
    "lifecycle=supported_host_pinned_until_process_exit\n"
    "minhook_commit=${PREYVR_MINHOOK_COMMIT}\n"
    "openxr_commit=${PREYVR_OPENXR_COMMIT}\n"
)
