cmake_minimum_required(VERSION 3.14)
project(PresenceUiPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

# Derive the module name from metadata.json — single source of truth.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" METADATA_JSON)
string(JSON MODULE_NAME GET ${METADATA_JSON} name)

# Universal UI module: the .rep view contract plus the *Backend class. The
# Qt-free core (key schedule, observer model, simulator) is plain C++ so it
# can be unit-tested natively and reused on mobile later.
logos_module(
    NAME ${MODULE_NAME}
    REP_FILE src/presence_ui.rep
    SOURCES
        src/presence_core.h
        src/presence_core.cpp
        src/presence_ui_backend.h
        src/presence_ui_backend.cpp
    INCLUDE_DIRS
        src
)
