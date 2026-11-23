# Each test is a Qt Test executable registered with CTest.
function(lumen_add_test name)
    qt_add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE FALSE)
    target_link_libraries(${name} PRIVATE Qt6::Test lumen_warnings)
    add_test(NAME ${name} COMMAND ${name})
    # Run without a display on every OS, with the software Qt Quick renderer.
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 60)
endfunction()

lumen_add_test(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE Lumen::Core)
target_compile_definitions(tst_core PRIVATE EXPECTED_VERSION="${PROJECT_VERSION}")

lumen_add_test(tst_mainwindow tst_mainwindow.cpp)
target_link_libraries(tst_mainwindow PRIVATE lumen_ui lumen_uiplugin Qt6::Quick)

# The render process stub must build and run on every platform.
add_test(NAME render_stub_runs COMMAND lumen-render --version)
set_tests_properties(render_stub_runs PROPERTIES
    PASS_REGULAR_EXPRESSION "lumen-render ${PROJECT_VERSION}"
    TIMEOUT 30)
