if(NOT DEFINED OPENOBFUSCATOR OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "Missing required CLI test path")
endif()

execute_process(
    COMMAND "${OPENOBFUSCATOR}" --version
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_stdout
    ERROR_VARIABLE version_stderr
)
if(NOT version_result EQUAL 0 OR NOT version_stdout STREQUAL "OpenObfuscator 1.3.0\n" OR NOT version_stderr STREQUAL "")
    message(FATAL_ERROR "--version contract failed: ${version_result} [${version_stdout}] [${version_stderr}]")
endif()

set(short_output "${OUTPUT_DIR}/cli-seed-short.lua")
set(long_output "${OUTPUT_DIR}/cli-seed-long.lua")
set(max_output "${OUTPUT_DIR}/cli-seed-max.lua")
execute_process(COMMAND "${OPENOBFUSCATOR}" -s 0 "${FIXTURE}" "${short_output}" RESULT_VARIABLE short_result ERROR_VARIABLE short_stderr)
execute_process(COMMAND "${OPENOBFUSCATOR}" --seed 0 "${FIXTURE}" "${long_output}" RESULT_VARIABLE long_result ERROR_VARIABLE long_stderr)
execute_process(COMMAND "${OPENOBFUSCATOR}" --seed 4294967295 "${FIXTURE}" "${max_output}" RESULT_VARIABLE max_result ERROR_VARIABLE max_stderr)
if(NOT short_result EQUAL 0 OR NOT long_result EQUAL 0 OR NOT max_result EQUAL 0)
    message(FATAL_ERROR "Valid seed invocation failed: short=${short_result} long=${long_result} max=${max_result}; ${short_stderr}${long_stderr}${max_stderr}")
endif()
file(READ "${short_output}" short_content)
file(READ "${long_output}" long_content)
if(NOT short_content STREQUAL long_content)
    message(FATAL_ERROR "-s and --seed produced different output")
endif()

foreach(invalid_seed IN ITEMS "-1" "12x" "4294967296")
    execute_process(
        COMMAND "${OPENOBFUSCATOR}" --seed "${invalid_seed}" "${FIXTURE}"
        RESULT_VARIABLE invalid_result
        OUTPUT_QUIET
        ERROR_VARIABLE invalid_stderr
    )
    if(invalid_result EQUAL 0 OR NOT invalid_stderr MATCHES "Invalid uint32 seed")
        message(FATAL_ERROR "Invalid seed '${invalid_seed}' was not rejected correctly: ${invalid_result} [${invalid_stderr}]")
    endif()
endforeach()

execute_process(COMMAND "${OPENOBFUSCATOR}" --seed RESULT_VARIABLE missing_seed_result OUTPUT_QUIET ERROR_VARIABLE missing_seed_stderr)
if(missing_seed_result EQUAL 0 OR NOT missing_seed_stderr MATCHES "Missing value for --seed")
    message(FATAL_ERROR "Missing --seed value was not rejected correctly")
endif()

execute_process(COMMAND "${OPENOBFUSCATOR}" -o RESULT_VARIABLE missing_output_result OUTPUT_QUIET ERROR_VARIABLE missing_output_stderr)
if(missing_output_result EQUAL 0 OR NOT missing_output_stderr MATCHES "Missing value for -o")
    message(FATAL_ERROR "Missing -o value was not rejected correctly")
endif()

execute_process(
    COMMAND "${OPENOBFUSCATOR}" "${FIXTURE}" "${OUTPUT_DIR}/extra.lua" unexpected
    RESULT_VARIABLE extra_result
    OUTPUT_QUIET
    ERROR_VARIABLE extra_stderr
)
if(extra_result EQUAL 0 OR NOT extra_stderr MATCHES "Unexpected argument")
    message(FATAL_ERROR "Unexpected positional argument was not rejected correctly")
endif()

message(STATUS "CLI version, seed boundaries, aliases, and argument errors passed")
