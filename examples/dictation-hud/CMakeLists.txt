find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM IMPORTED_TARGET gtkmm-3.0)

if (NOT GTKMM_FOUND)
    message(STATUS "dictation-hud: gtkmm-3.0 not found (install libgtkmm-3.0-dev); skipping whisper-dictation-hud")
    return()
endif()

set(TARGET whisper-dictation-hud)
add_executable(${TARGET} dictation-hud.cpp)

include(DefaultTargetOptions)

target_link_libraries(${TARGET} PRIVATE PkgConfig::GTKMM)

install(TARGETS ${TARGET} RUNTIME)
