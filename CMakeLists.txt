cmake_minimum_required(VERSION 3.20)
project(OpenUtauFX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The sources carry non-ASCII characters in comments (the faceplate glyphs).
if(MSVC)
  add_compile_options(/utf-8)
endif()

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/iPlug2" CACHE PATH "iPlug2 root")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

# Header-only DSP and UI headers, shared with the plugin target.
add_library(oufx_headers INTERFACE)
target_include_directories(oufx_headers INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/OpenUtauFX
)

# VST3, plus a standalone app for previewing the rack without a host.
iplug_add_plugin(OpenUtauFX
  SOURCES
    src/OpenUtauFX/OpenUtauFX.cpp
    src/OpenUtauFX/OpenUtauFX.h
    src/OpenUtauFX/config.h
    src/OpenUtauFX/RackTheme.h
    src/OpenUtauFX/RackControls.h
    src/OpenUtauFX/RackLayout.h
    src/OpenUtauFX/FxDisplays.h
    src/dsp/MixFxDSP.h
    src/dsp/MixFxPresets.h
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
    resources/fonts/Roboto-Bold.ttf
    resources/fonts/RobotoMono-Regular.ttf
  FORMATS VST3 APP
  LINK oufx_headers
)

# On Windows the fonts are embedded in the binaries as Win32 resources, which
# is how iPlug2's LocateResource finds them; only the standalone app target
# gets the .rc automatically, so the plug-in targets are wired up here.
if(WIN32)
  set(OUFX_RC "${CMAKE_CURRENT_SOURCE_DIR}/resources/main.rc")

  set_source_files_properties("${OUFX_RC}" PROPERTIES
    COMPILE_FLAGS "/I\"${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts\" /I\"${CMAKE_CURRENT_SOURCE_DIR}/resources/img\" /I\"${CMAKE_CURRENT_SOURCE_DIR}/resources\""
  )

  foreach(_target OpenUtauFX-vst3)
    if(TARGET ${_target})
      target_sources(${_target} PRIVATE "${OUFX_RC}")
    endif()
  endforeach()
endif()
