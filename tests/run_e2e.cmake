if(NOT DEFINED OPENOBFUSCATOR OR NOT DEFINED LUAJIT OR NOT DEFINED FIXTURE OR NOT DEFINED INTEGRITY_TEST OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "Missing required E2E test path")
endif()

execute_process(
    COMMAND "${LUAJIT}" "${FIXTURE}" first second
    RESULT_VARIABLE source_result
    OUTPUT_VARIABLE source_stdout
    ERROR_VARIABLE source_stderr
)
if(NOT source_result EQUAL 0)
    message(FATAL_ERROR "Source fixture failed (${source_result}): ${source_stderr}")
endif()

execute_process(
    COMMAND "${OPENOBFUSCATOR}" --seed 0 "${FIXTURE}" "${OUTPUT}"
    RESULT_VARIABLE obfuscator_result
    OUTPUT_VARIABLE obfuscator_stdout
    ERROR_VARIABLE obfuscator_stderr
)
if(NOT obfuscator_result EQUAL 0)
    message(FATAL_ERROR "Obfuscation failed (${obfuscator_result}): ${obfuscator_stderr}")
endif()

execute_process(
    COMMAND "${LUAJIT}" "${OUTPUT}" first second
    RESULT_VARIABLE output_result
    OUTPUT_VARIABLE output_stdout
    ERROR_VARIABLE output_stderr
)
if(NOT output_result EQUAL 0)
    message(FATAL_ERROR "Obfuscated fixture failed (${output_result}): ${output_stderr}")
endif()

set(expected_stdout "args=2;first=first\nescaped=quote:\" slash:\\ tab:\t\nlong=long:[[]]:literal|second\ncalc=13,42,49;numeric=31\nrelay=alpha,99,omega\n")
if(NOT source_stdout STREQUAL expected_stdout)
    message(FATAL_ERROR "Fixture stdout is not stable.\nExpected: [${expected_stdout}]\nActual: [${source_stdout}]")
endif()
if(NOT output_stdout STREQUAL source_stdout)
    message(FATAL_ERROR "Behavior differs.\nSource: [${source_stdout}]\nObfuscated: [${output_stdout}]")
endif()

execute_process(
    COMMAND "${LUAJIT}" "${INTEGRITY_TEST}" "${OUTPUT}"
    RESULT_VARIABLE integrity_result
    OUTPUT_VARIABLE integrity_stdout
    ERROR_VARIABLE integrity_stderr
)
if(NOT integrity_result EQUAL 0)
    message(FATAL_ERROR "VM integrity test failed (${integrity_result}): ${integrity_stderr}")
endif()
if(NOT integrity_stdout STREQUAL "VM rejects unknown opcodes, invalid HALT flow, payload tampering, and wrong length\n")
    message(FATAL_ERROR "Unexpected integrity test output: [${integrity_stdout}]")
endif()

message(STATUS "LuaJIT E2E behavior matches source and rejects tampering")
