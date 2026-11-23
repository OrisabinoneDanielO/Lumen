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
)
add_library(Lumen::Operator ALIAS lumen_operator)
target_include_directories(lumen_operator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lumen_operator
    PUBLIC Lumen::Data Lumen::Show Lumen::Songs Qt6::Core Qt6::Gui
    PRIVATE lumen_warnings)
