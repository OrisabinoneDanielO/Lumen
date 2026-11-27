# Tests run offscreen with the software Qt Quick renderer. On Windows the
# offscreen platform reads fonts from a folder
# (Qt's lib/fonts by default, which Qt no longer ships); give it Windows'.
set(lumen_test_env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software)
if(WIN32)
    file(TO_CMAKE_PATH "$ENV{WINDIR}/Fonts" windows_fonts)
    list(APPEND lumen_test_env "QT_QPA_FONTDIR=${windows_fonts}")
endif()

# Each test is a Qt Test executable registered with CTest.
function(lumen_add_test name)
    qt_add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE FALSE)
    target_link_libraries(${name} PRIVATE Qt6::Test lumen_warnings)
    add_test(NAME ${name} COMMAND ${name})
    # Run without a display on every OS, with the software Qt Quick renderer.
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "${lumen_test_env}"
        TIMEOUT 60)
endfunction()

# Test data shared by the operator tests: the mockup's sample service.
add_library(lumen_test_support STATIC support/sampleservice.cpp)
target_include_directories(lumen_test_support PUBLIC support)
target_link_libraries(lumen_test_support PUBLIC Lumen::Operator PRIVATE lumen_warnings)

lumen_add_test(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE Lumen::Core)
target_compile_definitions(tst_core PRIVATE EXPECTED_VERSION="${PROJECT_VERSION}")

lumen_add_test(tst_mainwindow tst_mainwindow.cpp)
target_link_libraries(tst_mainwindow PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick)

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
target_link_libraries(tst_operator PRIVATE Lumen::Operator lumen_test_support)

lumen_add_test(tst_operator_window tst_operator_window.cpp operatorharness.h)
target_link_libraries(tst_operator_window PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest)

# Done when: clicking or pressing Space sends a slide live in under 50 ms.
lumen_add_test(tst_send_live_latency tst_send_live_latency.cpp operatorharness.h)
target_link_libraries(tst_send_live_latency PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest)
target_compile_definitions(tst_send_live_latency PRIVATE LUMEN_RENDER_PATH="$<TARGET_FILE:lumen-render>")
add_dependencies(tst_send_live_latency lumen-render)

# Done when (batch 3): killing the UI process leaves the last frame on screen.
lumen_add_test(tst_render_survives_ui_kill tst_render_survives_ui_kill.cpp rendercheck.h)
target_link_libraries(tst_render_survives_ui_kill PRIVATE Lumen::RenderLink Qt6::Gui)
target_compile_definitions(tst_render_survives_ui_kill PRIVATE
    LUMEN_APP_PATH="$<TARGET_FILE:lumen>"
    LUMEN_RENDER_PATH="$<TARGET_FILE:lumen-render>")
add_dependencies(tst_render_survives_ui_kill lumen lumen-render)

# Song library (batch 6).
lumen_add_test(tst_song_import tst_song_import.cpp)
target_link_libraries(tst_song_import PRIVATE Lumen::Songs)

# Done when: 20 sample songs import and display correctly.
lumen_add_test(tst_song_samples tst_song_samples.cpp operatorharness.h)
target_link_libraries(tst_song_samples PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Lumen::Songs Qt6::Quick Qt6::QuickTest)
target_compile_definitions(tst_song_samples PRIVATE LUMEN_SONGS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/songs")

# Scripture (batch 7).
lumen_add_test(tst_scripture tst_scripture.cpp)
target_link_libraries(tst_scripture PRIVATE Lumen::Scripture)

# Done when: "jn 3 16-18" shows the right verses in every bundled Bible.
lumen_add_test(tst_scripture_bundled tst_scripture_bundled.cpp operatorharness.h)
target_link_libraries(tst_scripture_bundled PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest)
target_compile_definitions(tst_scripture_bundled PRIVATE
    LUMEN_BIBLES_DB="${LUMEN_BIBLES_DB}"
    LUMEN_BIBLES_MANIFEST="${CMAKE_SOURCE_DIR}/bibles/manifest.json")
add_dependencies(tst_scripture_bundled bibles)

# Themes (batch 8). Done when: long verses fit; diacritics render correctly.
lumen_add_test(tst_theme_fit tst_theme_fit.cpp)
target_link_libraries(tst_theme_fit PRIVATE lumen_render_lib Lumen::Operator lumen_test_support Qt6::QuickTest)
target_compile_definitions(tst_theme_fit PRIVATE LUMEN_BIBLES_DB="${LUMEN_BIBLES_DB}")
add_dependencies(tst_theme_fit bibles)
set_tests_properties(tst_theme_fit PROPERTIES TIMEOUT 180)

lumen_add_test(tst_themes tst_themes.cpp operatorharness.h)
target_link_libraries(tst_themes PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest)

# Media (batch 9).
lumen_add_test(tst_video_playback tst_video_playback.cpp)
target_link_libraries(tst_video_playback PRIVATE lumen_render_lib)
target_compile_definitions(tst_video_playback PRIVATE LUMEN_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/media")
set_tests_properties(tst_video_playback PROPERTIES TIMEOUT 120)

# Done when: a 2-hour loop plays with zero dropped frames on the reference PC.
# Routine runs play 2 minutes; set LUMEN_SOAK_SECONDS=7200 for the full run.
lumen_add_test(tst_video_soak tst_video_soak.cpp)
target_link_libraries(tst_video_soak PRIVATE Lumen::RenderLink Lumen::Core)
target_compile_definitions(tst_video_soak PRIVATE
    LUMEN_RENDER_PATH="$<TARGET_FILE:lumen-render>"
    LUMEN_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/media")
add_dependencies(tst_video_soak lumen-render)
set_tests_properties(tst_video_soak PROPERTIES TIMEOUT 300)

# Requirement: a video preloaded in Preview starts in under 200 ms.
lumen_add_test(tst_video_start tst_video_start.cpp)
target_link_libraries(tst_video_start PRIVATE Lumen::RenderLink Lumen::Core)
target_compile_definitions(tst_video_start PRIVATE
    LUMEN_RENDER_PATH="$<TARGET_FILE:lumen-render>"
    LUMEN_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/media")
add_dependencies(tst_video_start lumen-render)

# Importing media, media slides, motion backgrounds and the Media tab.
lumen_add_test(tst_media tst_media.cpp operatorharness.h)
target_link_libraries(tst_media PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest)
target_compile_definitions(tst_media PRIVATE LUMEN_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/media")
set_tests_properties(tst_media PROPERTIES TIMEOUT 120)

# Presentations (batch 10). Done when: sample decks import with the right
# slide count and order.
lumen_add_test(tst_presentations tst_presentations.cpp operatorharness.h)
target_link_libraries(tst_presentations PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest)
target_compile_definitions(tst_presentations PRIVATE LUMEN_DECKS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/decks")
set_tests_properties(tst_presentations PROPERTIES TIMEOUT 300)

# Service plans, .lumen files, autosave and backups (batch 11).
# Done when: a plan survives a crash and restores from backup.
lumen_add_test(tst_plans tst_plans.cpp operatorharness.h)
target_link_libraries(tst_plans PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest)
target_compile_definitions(tst_plans PRIVATE
    LUMEN_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/media"
    LUMEN_DECKS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/decks")
set_tests_properties(tst_plans PROPERTIES TIMEOUT 180)

# Timers and basic messages (batch 12).
lumen_add_test(tst_timers tst_timers.cpp operatorharness.h rendercheck.h)
target_link_libraries(tst_timers PRIVATE lumen_render_lib lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest)

# The MVP stand-in (batch 12): a whole scripted service on the real renderer
# with zero output failures. LUMEN_REHEARSAL_SECONDS=3600 for a full hour.
lumen_add_test(tst_rehearsal tst_rehearsal.cpp operatorharness.h)
target_link_libraries(tst_rehearsal PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Lumen::RenderLink Qt6::Quick Qt6::QuickTest)
target_compile_definitions(tst_rehearsal PRIVATE
    LUMEN_RENDER_PATH="$<TARGET_FILE:lumen-render>"
    LUMEN_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/media"
    LUMEN_DECKS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/decks")
add_dependencies(tst_rehearsal lumen-render)
set_tests_properties(tst_rehearsal PROPERTIES TIMEOUT 300)

# Offline check (batch 12, Linux): the app and renderer open no internet
# sockets, and run with no network at all.
if(UNIX AND NOT APPLE)
    add_test(NAME offline_check COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/offline_check.sh $<TARGET_FILE:lumen>)
    set_tests_properties(offline_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endif()

# Stage display (batch 13). Done when: current, next, clock and message show
# on a second screen.
lumen_add_test(tst_stage tst_stage.cpp operatorharness.h rendercheck.h)
target_link_libraries(tst_stage PRIVATE lumen_render_lib lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Lumen::RenderLink Qt6::Quick Qt6::QuickTest)
target_compile_definitions(tst_stage PRIVATE LUMEN_RENDER_PATH="$<TARGET_FILE:lumen-render>")
add_dependencies(tst_stage lumen-render)

# Props, lower thirds and messages with fields (batch 14). Done when: props
# persist across slides and clear independently.
lumen_add_test(tst_props tst_props.cpp operatorharness.h rendercheck.h)
target_link_libraries(tst_props PRIVATE lumen_render_lib lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest)
target_compile_definitions(tst_props PRIVATE LUMEN_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/media")

# Stream output with alpha, key and fill, NDI plugin (batch 15). Done when:
# the stream output shows only Slide and Props on transparency.
lumen_add_test(tst_stream tst_stream.cpp rendercheck.h)
target_link_libraries(tst_stream PRIVATE lumen_render_lib Lumen::RenderLink Lumen::Show Lumen::Operator lumen_test_support)
target_compile_definitions(tst_stream PRIVATE
    LUMEN_RENDER_PATH="$<TARGET_FILE:lumen-render>"
    LUMEN_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/media")
add_dependencies(tst_stream lumen-render)

# The slide editor (batch 16). Done when: every element type can be added,
# styled and saved.
lumen_add_test(tst_editor tst_editor.cpp operatorharness.h rendercheck.h)
target_link_libraries(tst_editor PRIVATE lumen_render_lib lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest)
target_compile_definitions(tst_editor PRIVATE LUMEN_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/media")

# Importing from EasyWorship, ProPresenter and OpenLP (batch 17). Done when:
# libraries from each import without errors.
lumen_add_test(tst_foreign_import tst_foreign_import.cpp)
target_link_libraries(tst_foreign_import PRIVATE Lumen::Songs)
target_compile_definitions(tst_foreign_import PRIVATE LUMEN_LIBRARIES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/libraries")

# Library and plan sharing on the local network (batch 18). Done when: two
# PCs exchange a plan with no internet connection.
lumen_add_test(tst_sharing tst_sharing.cpp)
target_link_libraries(tst_sharing PRIVATE Lumen::Operator lumen_test_support Qt6::Test)
if(UNIX AND NOT APPLE)
    # The same, with no network at all but loopback.
    add_test(NAME tst_sharing_offline COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/no_network.sh $<TARGET_FILE:tst_sharing>)
    set_tests_properties(tst_sharing_offline PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Phone remote, MIDI and OSC (batch 19). Done when: a remote and a MIDI
# controller drive slides on the LAN.
lumen_add_test(tst_remote tst_remote.cpp operatorharness.h)
target_link_libraries(tst_remote PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest Qt6::Network)
if(TARGET lumen_rtmidi)
    target_link_libraries(tst_remote PRIVATE lumen_rtmidi)
    target_compile_definitions(tst_remote PRIVATE LUMEN_HAVE_MIDI)
endif()

# Timecode cues (batch 20). Done when: slides follow a backing track to the
# frame.
lumen_add_test(tst_timecode tst_timecode.cpp operatorharness.h)
target_link_libraries(tst_timecode PRIVATE lumen_render_lib lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest)
set_tests_properties(tst_timecode PROPERTIES TIMEOUT 120)

# The installed-layout check (lumen --check-install) from the build folder,
# where lumen-render and bibles.db sit beside lumen as in an install.
add_test(NAME lumen_check_install COMMAND lumen --check-install)
set_tests_properties(lumen_check_install PROPERTIES
    ENVIRONMENT "${lumen_test_env}"
    TIMEOUT 60)

# Speed checks against the reference hardware (support/timing.h): CI's macOS
# job runs these separately, as advisory.
set_tests_properties(tst_render_scene tst_video_start tst_video_soak tst_rehearsal tst_timecode PROPERTIES LABELS timing)

# The screen setup: which screen is the main output and which the stage
# display, on three virtual screens; the renderer moves and labels itself.
lumen_add_test(tst_screens tst_screens.cpp operatorharness.h)
target_link_libraries(tst_screens PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Lumen::RenderLink Qt6::Quick Qt6::QuickTest)
target_compile_definitions(tst_screens PRIVATE
    LUMEN_RENDER_PATH="$<TARGET_FILE:lumen-render>"
    LUMEN_APP_PATH="$<TARGET_FILE:lumen>")
add_dependencies(tst_screens lumen-render lumen)
set_tests_properties(tst_screens PROPERTIES TIMEOUT 120)

# Optional update checks: off until asked; newer, same, older, no release,
# nonsense and no answer, against a local stand-in for GitHub.
lumen_add_test(tst_updates tst_updates.cpp operatorharness.h)
target_link_libraries(tst_updates PRIVATE lumen_ui lumen_uiplugin Lumen::Operator lumen_test_support Qt6::Quick Qt6::QuickTest Qt6::Network)

# MIDI timecode: arithmetic (drop-frame too) and the quarter-frame decoder.
lumen_add_test(tst_mtc tst_mtc.cpp)
target_link_libraries(tst_mtc PRIVATE Lumen::Core)

# Timecode cues following MIDI timecode: frame by frame on a stepped clock,
# stopping, locating back, and a real virtual MIDI device.
lumen_add_test(tst_mtc_cues tst_mtc_cues.cpp)
target_link_libraries(tst_mtc_cues PRIVATE lumen_render_lib Qt6::Quick)
if(TARGET lumen_rtmidi)
    target_link_libraries(tst_mtc_cues PRIVATE lumen_rtmidi)
    target_compile_definitions(tst_mtc_cues PRIVATE LUMEN_HAVE_MIDI)
endif()

# Chords in lyrics and transposition.
lumen_add_test(tst_chords tst_chords.cpp)
target_link_libraries(tst_chords PRIVATE Lumen::Songs)
