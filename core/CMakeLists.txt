# Core services: build info (lumen_core) and the data layer (lumen_data).
# The render process links only lumen_core, so it never depends on Qt SQL.

add_library(lumen_core STATIC
    include/lumen/core/buildinfo.h
    include/lumen/core/result.h
    include/lumen/core/render/protocol.h
    include/lumen/core/show/scene.h
    src/buildinfo.cpp
    src/render/protocol.cpp
    src/show/scene.cpp
)
add_library(Lumen::Core ALIAS lumen_core)

target_include_directories(lumen_core PUBLIC include)
target_compile_definitions(lumen_core PRIVATE LUMEN_VERSION="${PROJECT_VERSION}")
target_link_libraries(lumen_core
    PUBLIC Qt6::Core Qt6::Gui
    PRIVATE lumen_warnings)

# Data layer: SQLite schema, migrations and repositories.
add_library(lumen_data STATIC
    include/lumen/core/data/database.h
    include/lumen/core/data/entities.h
    include/lumen/core/data/repository.h
    src/data/database.cpp
    src/data/repository.cpp
    src/data/sqlhelpers.h
)
add_library(Lumen::Data ALIAS lumen_data)

target_include_directories(lumen_data PUBLIC include)
target_link_libraries(lumen_data
    PUBLIC lumen_core Qt6::Sql
    PRIVATE lumen_warnings)

# The operator UI's link to the render process (local socket client).
add_library(lumen_renderlink STATIC
    include/lumen/core/render/renderlink.h
    src/render/renderlink.cpp
)
add_library(Lumen::RenderLink ALIAS lumen_renderlink)
target_link_libraries(lumen_renderlink
    PUBLIC lumen_core Qt6::Gui Qt6::Network
    PRIVATE lumen_warnings)

# Show controller: what is on air, and the bridge that sends it to a renderer.
add_library(lumen_show STATIC
    include/lumen/core/show/showcontroller.h
    include/lumen/core/show/outputbridge.h
    src/show/showcontroller.cpp
    src/show/outputbridge.cpp
)
add_library(Lumen::Show ALIAS lumen_show)
target_link_libraries(lumen_show
    PUBLIC lumen_core lumen_renderlink
    PRIVATE lumen_warnings)

# Song library: plain-text and OpenLyrics import, auto-split, search.
add_library(lumen_songs STATIC
    include/lumen/core/songs/songimport.h
    include/lumen/core/songs/songlibrary.h
    src/songs/songimport.cpp
    src/songs/songlibrary.cpp
)
add_library(Lumen::Songs ALIAS lumen_songs)
target_link_libraries(lumen_songs
    PUBLIC lumen_data
    PRIVATE lumen_warnings)

# Migrations are numbered SQL files compiled in as resources.
qt_add_resources(lumen_data "lumen_migrations"
    PREFIX "/lumen/migrations"
    BASE src/data/migrations
    FILES
        src/data/migrations/0001_initial.sql
        src/data/migrations/0002_song_search.sql
)
