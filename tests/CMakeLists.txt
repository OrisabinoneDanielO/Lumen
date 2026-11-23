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
target_link_libraries(tst_mainwindow PRIVATE lumen_ui lumen_uiplugin Lumen::Operator Qt6::Quick)

lumen_add_test(tst_database tst_database.cpp)
target_link_libraries(tst_database PRIVATE Lumen::Data)

lumen_add_test(tst_repositories tst_repositories.cpp)
target_link_libraries(tst_repositories PRIVATE Lumen::Data)

# The render process executable must build and run on every platform.
# (Since batch 3 it is a GUI app, so it needs the offscreen platform here.)
add_test(NAME render_stub_runs COMMAND lumen-render --version)
set_tests_properties(render_stub_runs PROPERTIES
    PASS_REGULAR_EXPRESSION "lumen-render ${PROJECT_VERSION}"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30)

# Render process (batch 3).
lumen_add_test(tst_screen_choice tst_screen_choice.cpp)
target_link_libraries(tst_screen_choice PRIVATE lumen_render_lib)

lumen_add_test(tst_render_scene tst_render_scene.cpp rendercheck.h)
target_link_libraries(tst_render_scene PRIVATE lumen_render_lib)

lumen_add_test(tst_render_server tst_render_server.cpp rendercheck.h)
target_link_libraries(tst_render_server PRIVATE lumen_render_lib)

# Show controller and layers (batch 4).
lumen_add_test(tst_show_controller tst_show_controller.cpp)
target_link_libraries(tst_show_controller PRIVATE Lumen::Show)

lumen_add_test(tst_render_layers tst_render_layers.cpp rendercheck.h)
target_link_libraries(tst_render_layers PRIVATE lumen_render_lib)

# Done when: scripted commands produce the expected frames.
lumen_add_test(tst_show_script tst_show_script.cpp rendercheck.h)
target_link_libraries(tst_show_script PRIVATE Lumen::Show Qt6::Gui)
target_compile_definitions(tst_show_script PRIVATE
    LUMEN_RENDER_PATH="$<TARGET_FILE:lumen-render>"
    LUMEN_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts")
add_dependencies(tst_show_script lumen-render)

# Operator UI shell (batch 5).
lumen_add_test(tst_operator tst_operator.cpp)
target_link_libraries(tst_operator PRIVATE Lumen::Operator)

lumen_add_test(tst_operator_window tst_operator_window.cpp operatorharness.h)
target_link_libraries(tst_operator_window PRIVATE lumen_ui lumen_uiplugin Lumen::Operator Qt6::Quick)

# Done when: clicking or pressing Space sends a slide live in under 50 ms.
lumen_add_test(tst_send_live_latency tst_send_live_latency.cpp operatorharness.h)
target_link_libraries(tst_send_live_latency PRIVATE lumen_ui lumen_uiplugin Lumen::Operator Qt6::Quick)
target_compile_definitions(tst_send_live_latency PRIVATE LUMEN_RENDER_PATH="$<TARGET_FILE:lumen-render>")
add_dependencies(tst_send_live_latency lumen-render)

# Done when (batch 3): killing the UI process leaves the last frame on screen.
lumen_add_test(tst_render_survives_ui_kill tst_render_survives_ui_kill.cpp rendercheck.h)
target_link_libraries(tst_render_survives_ui_kill PRIVATE Lumen::RenderLink Qt6::Gui)
target_compile_definitions(tst_render_survives_ui_kill PRIVATE
    LUMEN_APP_PATH="$<TARGET_FILE:lumen>"
    LUMEN_RENDER_PATH="$<TARGET_FILE:lumen-render>")
add_dependencies(tst_render_survives_ui_kill lumen lumen-render)
