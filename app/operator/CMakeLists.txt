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
)
add_library(Lumen::Operator ALIAS lumen_operator)
target_include_directories(lumen_operator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lumen_operator
    PUBLIC Lumen::Data Lumen::Show Lumen::Songs Lumen::Scripture Qt6::Core Qt6::Gui Qt6::Multimedia
    PRIVATE Qt6::Pdf Qt6::Xml Qt6::GuiPrivate lumen_warnings)
