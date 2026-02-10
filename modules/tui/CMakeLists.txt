set(TUI_SOURCES
    src/commands.cpp
    src/command_bar.cpp
    src/managers/command_manager.cpp
    src/managers/key_manager.cpp
    src/managers/layout.cpp
    src/managers/menu_manager.cpp
    src/managers/pane_reaper.cpp
    src/managers/mode_manager.cpp
    src/menu_registry.cpp
    src/pane.cpp
    src/statusbar.cpp
    src/terminal.cpp
    src/theme.cpp
    src/tui.cpp
    src/tui_context.cpp
    src/window.cpp
    src/menus/docker_menu.cpp
    src/menus/main_menu.cpp
)

add_library(tui STATIC ${TUI_SOURCES})

# Includes
set(TUI_INCLUDE_DIRS
    include
)

include(GNUInstallDirs)
install(TARGETS tui
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
