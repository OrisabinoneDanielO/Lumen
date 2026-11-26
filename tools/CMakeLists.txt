# Build-time tools.

# Imports the bundled Bibles into bibles.db (see bibles/README.md).
qt_add_executable(lumen-bible-build lumen-bible-build.cpp)
target_link_libraries(lumen-bible-build PRIVATE Lumen::Scripture lumen_warnings)
