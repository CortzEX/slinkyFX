if(NOT WIN32)
    # ------------------------------------------------------------------
    # Linux loader: same behaviour as the Windows build (find the game,
    # inject slinky-native, monitor the log), implemented over /proc,
    # X11 and ptrace in src/linux/.
    # ------------------------------------------------------------------
    file(GLOB SLINKY_LINUX_SRC CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/linux/*.cpp")
    add_executable(Slinky ${SLINKY_LINUX_SRC})

    find_package(Threads REQUIRED)

    # X11 is optional: the loader finds the game through /proc, which works
    # under Wayland without any X11 tooling. When the X11 headers/libs are
    # present we also match windows by title (X11 and XWayland sessions).
    find_package(X11 QUIET)

    target_include_directories(Slinky PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/linux
    )
    target_link_libraries(Slinky PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    if(X11_FOUND)
        target_compile_definitions(Slinky PRIVATE SLINKY_HAVE_X11)
        target_link_libraries(Slinky PRIVATE X11::X11)
        message(STATUS "slinkyFX: X11 found - window-title matching enabled")
    else()
        message(STATUS "slinkyFX: X11 not found - using /proc detection only (Wayland friendly)")
    endif()

    set_target_properties(Slinky PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        OUTPUT_NAME "slinky-loader"
    )
    return()
endif()

add_executable(Slinky
    src/Main.cpp
    src/Injector.cpp
    src/ProcessUtil.cpp
    src/ConsoleUI.cpp
    Slinky.rc
)

target_include_directories(Slinky PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(Slinky PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
)

set_target_properties(Slinky PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "slinky-loader"
)
