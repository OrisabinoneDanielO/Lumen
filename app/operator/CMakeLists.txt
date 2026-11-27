# C++ glue between the operator window and the core: reads the plan from the
# library, drives the show controller, and maps keys to live actions.
add_library(lumen_operator STATIC
    servicedata.h
    servicedata.cpp
    keymap.h
    keymap.cpp
    rowmodel.h
    rowmodel.cpp
    operatorcontroller.h
    operatorcontroller.cpp
    usagelog.h
    usagelog.cpp
    language.h
    language.cpp
    scripturecontroller.h
    scripturecontroller.cpp
    themes.h
    themes.cpp
    themecontroller.h
    themecontroller.cpp
    medialibrary.h
    medialibrary.cpp
    pptxreader.h
    pptxreader.cpp
    deckimporter.h
    deckimporter.cpp
    presentationlibrary.h
    presentationlibrary.cpp
    planstore.h
    planstore.cpp
    backups.h
    backups.cpp
    lumenfile.h
    lumenfile.cpp
    timercontroller.h
    timercontroller.cpp
    updatechecker.h
    updatechecker.cpp
    screensetup.h
    screensetup.cpp
    stagecontroller.h
    stagecontroller.cpp
    propscontroller.h
    propscontroller.cpp
    messagetemplates.h
    messagetemplates.cpp
    streamcontroller.h
    streamcontroller.cpp
    editorcontroller.h
    editorcontroller.cpp
    sharing.h
    sharing.cpp
    livecontrol.h
    livecontrol.cpp
    remoteserver.h
    remoteserver.cpp
    oscserver.h
    oscserver.cpp
    midiinput.h
    midiinput.cpp
    cuecontroller.h
    cuecontroller.cpp
)
# The phone remote's page (batch 19).
qt_add_resources(lumen_operator "remote_page"
    PREFIX "/lumen/remote"
    BASE remote
    FILES remote/remote.html)
# MIDI control needs RtMidi (thirdparty/), built when a MIDI backend exists.
if(TARGET lumen_rtmidi)
    target_link_libraries(lumen_operator PRIVATE lumen_rtmidi)
    target_compile_definitions(lumen_operator PRIVATE LUMEN_HAVE_MIDI)
endif()
add_library(Lumen::Operator ALIAS lumen_operator)
target_include_directories(lumen_operator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lumen_operator
    PUBLIC Lumen::Data Lumen::Show Lumen::MtcInput Lumen::Songs Lumen::Scripture Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Network
    PRIVATE Qt6::Pdf Qt6::Xml Qt6::GuiPrivate Qt6::Qml lumen_warnings)
