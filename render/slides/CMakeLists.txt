# Lumen.Slides: how slide content is drawn, shared by the render process and
# the operator UI's Preview and Live frames, so both look the same.
qt_add_library(lumen_slides STATIC)
qt_add_qml_module(lumen_slides
    URI Lumen.Slides
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Lumen/Slides
    QML_FILES
        SlideContent.qml
        ComposedSlide.qml
        timertext.js
)
target_link_libraries(lumen_slides PUBLIC Qt6::Quick Qt6::Multimedia PRIVATE lumen_warnings)
