# Core services: data layer, show controller and importers (filled in from batch 2).
add_library(lumen_core STATIC
    include/lumen/core/buildinfo.h
    src/buildinfo.cpp
)
add_library(Lumen::Core ALIAS lumen_core)

target_include_directories(lumen_core PUBLIC include)
target_compile_definitions(lumen_core PRIVATE LUMEN_VERSION="${PROJECT_VERSION}")
target_link_libraries(lumen_core
    PUBLIC Qt6::Core
    PRIVATE lumen_warnings)
