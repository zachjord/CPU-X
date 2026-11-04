project(tests
	LANGUAGES NONE
)

add_test(NAME test_awk_regex  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/awk/test_regex.sh")
add_test(NAME test_grep_regex COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/grep/test_regex.sh")

add_executable(test_package_match package_match.cpp)
target_include_directories(test_package_match PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME test_package_match COMMAND test_package_match)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME test_package_database
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_package_database.py)
    add_custom_target(check_package_database
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/generate_package_database.py" --check)
endif()
