# Render process. Batch 1 builds only a stub so the separate executable exists
# on every platform; batch 3 adds the RHI output window.
qt_add_executable(lumen-render main.cpp)
target_link_libraries(lumen-render PRIVATE
    Lumen::Core
    Qt6::Core
    lumen_warnings)

include(GNUInstallDirs)
install(TARGETS lumen-render RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
