add_executable(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE advcore Qt6::Test)
target_compile_definitions(tst_core PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    REPO_DIR="${CMAKE_SOURCE_DIR}"
)
add_test(NAME tst_core COMMAND tst_core)
