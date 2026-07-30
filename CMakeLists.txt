cmake_minimum_required(VERSION 3.16)

project(OpenObfuscator VERSION 1.1.0 LANGUAGES CXX)

add_library(openobfuscator_core
    src/luaobf.cpp
)
target_include_directories(openobfuscator_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include/openobfuscator>
)
target_compile_features(openobfuscator_core PUBLIC cxx_std_17)
set_target_properties(openobfuscator_core PROPERTIES CXX_EXTENSIONS OFF)

add_executable(openobfuscator
    src/main.cpp
    src/gui.cpp
)
target_link_libraries(openobfuscator PRIVATE openobfuscator_core)
target_compile_features(openobfuscator PRIVATE cxx_std_17)
set_target_properties(openobfuscator PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
    target_link_libraries(openobfuscator PRIVATE comdlg32 gdi32 user32)
endif()
if(MINGW)
    target_link_options(openobfuscator PRIVATE -static)
endif()

include(CTest)
option(OPENOBFUSCATOR_REQUIRE_LUAJIT_TESTS
    "Fail configuration when LuaJIT runtime tests cannot be registered"
    OFF
)
if(BUILD_TESTING)
    add_executable(openobfuscator_tests tests/test_obfuscator.cpp)
    target_link_libraries(openobfuscator_tests PRIVATE openobfuscator_core)
    target_compile_features(openobfuscator_tests PRIVATE cxx_std_17)
    set_target_properties(openobfuscator_tests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME openobfuscator_unit COMMAND openobfuscator_tests)
    add_test(
        NAME openobfuscator_cli
        COMMAND ${CMAKE_COMMAND}
            -DOPENOBFUSCATOR=$<TARGET_FILE:openobfuscator>
            -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixture.lua
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cli_tests.cmake
    )

    find_program(LUAJIT_EXECUTABLE NAMES luajit luajit-2.1 luajit-2.0)
    if(LUAJIT_EXECUTABLE)
        add_test(
            NAME openobfuscator_e2e
            COMMAND ${CMAKE_COMMAND}
                -DOPENOBFUSCATOR=$<TARGET_FILE:openobfuscator>
                -DLUAJIT=${LUAJIT_EXECUTABLE}
                -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixture.lua
                -DINTEGRITY_TEST=${CMAKE_CURRENT_SOURCE_DIR}/tests/test_integrity.lua
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/e2e-obfuscated.lua
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_e2e.cmake
        )
    elseif(OPENOBFUSCATOR_REQUIRE_LUAJIT_TESTS)
        message(FATAL_ERROR "LuaJIT is required because OPENOBFUSCATOR_REQUIRE_LUAJIT_TESTS=ON")
    else()
        message(STATUS "LuaJIT not found; openobfuscator_e2e test skipped")
    endif()
endif()

install(TARGETS openobfuscator openobfuscator_core
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(FILES
    src/luaobf.h
    DESTINATION include/openobfuscator
)
install(FILES
    README.md
    CHANGELOG.md
    LICENSE
    DESTINATION share/openobfuscator
)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "OpenObfuscator")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
