# Core services: build info (lumen_core) and the data layer (lumen_data).
# The render process links only lumen_core, so it never depends on Qt SQL.

add_library(lumen_core STATIC
    include/lumen/core/buildinfo.h
    include/lumen/core/result.h
    src/buildinfo.cpp
)
add_library(Lumen::Core ALIAS lumen_core)

target_include_directories(lumen_core PUBLIC include)
target_compile_definitions(lumen_core PRIVATE LUMEN_VERSION="${PROJECT_VERSION}")
target_link_libraries(lumen_core
    PUBLIC Qt6::Core
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

# Migrations are numbered SQL files compiled in as resources.
qt_add_resources(lumen_data "lumen_migrations"
    PREFIX "/lumen/migrations"
    BASE src/data/migrations
    FILES
        src/data/migrations/0001_initial.sql
)
