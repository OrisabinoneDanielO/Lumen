# Bundled Bibles: check each package against manifest.json, unpack it, and
# import them all into bin/bibles.db with lumen-bible-build.

set(manifest ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json)
set(unpacked ${CMAKE_BINARY_DIR}/bibles-src)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${manifest})

file(READ ${manifest} manifest_json)
string(JSON count LENGTH "${manifest_json}" translations)
math(EXPR last "${count} - 1")
set(packages)
foreach(i RANGE ${last})
    string(JSON id GET "${manifest_json}" translations ${i} id)
    string(JSON expected GET "${manifest_json}" translations ${i} sha256)
    set(zip ${CMAKE_CURRENT_SOURCE_DIR}/${id}/${id}_usfm.zip)
    file(SHA256 ${zip} actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Bible package ${zip} does not match its SHA-256 in manifest.json")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${zip})
    if(NOT EXISTS ${unpacked}/${id}/.sha256 OR NOT EXISTS ${unpacked}/${id})
        file(REMOVE_RECURSE ${unpacked}/${id})
        file(ARCHIVE_EXTRACT INPUT ${zip} DESTINATION ${unpacked}/${id})
        file(WRITE ${unpacked}/${id}/.sha256 ${actual})
    else()
        file(READ ${unpacked}/${id}/.sha256 unpacked_sha)
        if(NOT unpacked_sha STREQUAL actual)
            file(REMOVE_RECURSE ${unpacked}/${id})
            file(ARCHIVE_EXTRACT INPUT ${zip} DESTINATION ${unpacked}/${id})
            file(WRITE ${unpacked}/${id}/.sha256 ${actual})
        endif()
    endif()
    list(APPEND packages ${zip})
endforeach()

set(LUMEN_BIBLES_DB ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/bibles.db CACHE INTERNAL "Built Bible database")
add_custom_command(
    OUTPUT ${LUMEN_BIBLES_DB}
    COMMAND lumen-bible-build --manifest ${manifest} --source ${unpacked} --output ${LUMEN_BIBLES_DB}
    DEPENDS lumen-bible-build ${manifest} ${packages}
    COMMENT "Importing the bundled Bibles into bibles.db"
    VERBATIM)
add_custom_target(bibles ALL DEPENDS ${LUMEN_BIBLES_DB})

install(FILES ${LUMEN_BIBLES_DB} DESTINATION ${CMAKE_INSTALL_DATADIR}/lumen)
