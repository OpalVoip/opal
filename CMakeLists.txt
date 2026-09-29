# PTLib is a required dependency. Point CMake at an install prefix or a
# PTLib CMake build directory:
#   cmake -B build -DOPAL_PTLIB_DIR=<ptlib-prefix-or-build>
#
# Optional codec libraries can come from vcpkg. Point CMake at a vcpkg checkout:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
# Default manifest features are libsrtp and speexdsp. Other features are
# listed in vcpkg.json.
#
# The default target is Release. Other targets:
#   cmake --build build --target debug   # Debug tree in build/debug
#   cmake --build build --target both     # Release and Debug
cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.h" _opal_version_h)
string(REGEX MATCH "#define MAJOR_VERSION ([0-9]+)" _ "${_opal_version_h}")
set(OPAL_VERSION_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define MINOR_VERSION ([0-9]+)" _ "${_opal_version_h}")
set(OPAL_VERSION_MINOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define PATCH_VERSION ([0-9]+)" _ "${_opal_version_h}")
set(OPAL_VERSION_PATCH "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define OEM_VERSION ([0-9]+)" _ "${_opal_version_h}")
set(OPAL_VERSION_OEM "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define BUILD_TYPE ([A-Za-z]+)" _ "${_opal_version_h}")
set(_opal_build_type "${CMAKE_MATCH_1}")

set(OPAL_VERSION "${OPAL_VERSION_MAJOR}.${OPAL_VERSION_MINOR}.${OPAL_VERSION_PATCH}")
if(_opal_build_type STREQUAL "AlphaCode")
  string(APPEND OPAL_VERSION "-alpha")
elseif(_opal_build_type STREQUAL "BetaCode")
  string(APPEND OPAL_VERSION "-beta")
endif()

# vcpkg installs manifest dependencies when its toolchain loads inside project().
# A classic "vcpkg install wxwidgets" is not on that search path. OpenPhone needs
# the wxwidgets feature, so turn it on before that install runs.
if(OPAL_BUILD_SAMPLES)
  set(_opal_vcpkg_features "${VCPKG_MANIFEST_FEATURES}")
  if(NOT _opal_vcpkg_features AND DEFINED ENV{VCPKG_MANIFEST_FEATURES})
    set(_opal_vcpkg_features "$ENV{VCPKG_MANIFEST_FEATURES}")
  endif()
  string(REPLACE "," ";" _opal_vcpkg_features "${_opal_vcpkg_features}")
  if(NOT "wxwidgets" IN_LIST _opal_vcpkg_features)
    list(APPEND _opal_vcpkg_features wxwidgets)
  endif()
  set(VCPKG_MANIFEST_FEATURES "${_opal_vcpkg_features}")
  unset(_opal_vcpkg_features)
endif()

project(OPAL
  VERSION "${OPAL_VERSION_MAJOR}.${OPAL_VERSION_MINOR}.${OPAL_VERSION_PATCH}"
  DESCRIPTION "Open Phone Abstraction Library"
  LANGUAGES C CXX)

# Single-config generators (Makefiles, Ninja) have no build type until one is
# chosen. Release stays the default target; debug and both are added below.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
    "Build type for the default target. The debug and both targets build Debug separately."
    FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Release Debug RelWithDebInfo MinSizeRel)
endif()
if(CMAKE_GENERATOR STREQUAL "Ninja Multi-Config" AND NOT CMAKE_DEFAULT_BUILD_TYPE)
  set(CMAKE_DEFAULT_BUILD_TYPE Release CACHE STRING
    "Configuration built by default for the Ninja Multi-Config generator." FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT DEFINED BUILD_SHARED_LIBS)
  set(BUILD_SHARED_LIBS ON)
endif()

set(OPAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(cmake/OPALConfigHeader.cmake)

set(OPAL_PTLIB_DIR "" CACHE PATH
  "PTLib install prefix, or a PTLib CMake build directory.")
if(OPAL_PTLIB_DIR)
  if(EXISTS "${OPAL_PTLIB_DIR}/PTLibConfig.cmake")
    set(PTLib_DIR "${OPAL_PTLIB_DIR}")
  else()
    list(PREPEND CMAKE_PREFIX_PATH "${OPAL_PTLIB_DIR}")
  endif()
endif()
find_package(PTLib CONFIG REQUIRED)
find_package(PkgConfig QUIET)

include(cmake/OPALOptions.cmake)
include(cmake/OPALSources.cmake)

find_package(Git QUIET)
set(OPAL_GIT_COMMIT "")
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${OPAL_SOURCE_DIR}"
    OUTPUT_VARIABLE OPAL_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/include/revision.h"
"#ifndef _OPAL_REVISION_H
#define _OPAL_REVISION_H
#define SVN_REVISION 0
#define GIT_COMMIT \"${OPAL_GIT_COMMIT}\"
#endif
")

opal_define(OPAL_MAJOR ${OPAL_VERSION_MAJOR})
opal_define(OPAL_MINOR ${OPAL_VERSION_MINOR})
opal_define(OPAL_PATCH ${OPAL_VERSION_PATCH})
opal_define(OPAL_OEM ${OPAL_VERSION_OEM})
opal_define(OPAL_VERSION "\"${OPAL_VERSION}\"")

set(OPAL_PLUGIN_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/opal-${OPAL_VERSION}")
if(OPAL_PLUGINS AND WIN32 AND (MINGW OR CYGWIN))
  set(OPAL_PLUGIN_DIR "plugins")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")
opal_write_config_header(
  "${OPAL_SOURCE_DIR}/include/opal_config.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/opal_config.h")

add_library(opal ${OPAL_SOURCES})
set_target_properties(opal PROPERTIES
  OUTPUT_NAME opal
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${PROJECT_VERSION_MAJOR}")
target_include_directories(opal
  PUBLIC
    "$<BUILD_INTERFACE:${OPAL_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE
    "${OPAL_SOURCE_DIR}/src"
    "${OPAL_SOURCE_DIR}/src/sdp/BFCP/libbfcp"
    "${OPAL_SOURCE_DIR}/src/codec/speex/libspeex")
target_compile_definitions(opal PRIVATE "OPAL_PLUGIN_DIR=\"${OPAL_PLUGIN_DIR}\"")
target_link_libraries(opal PUBLIC PTLib::ptlib)
if(OPAL_SRTP_LINK)
  target_link_libraries(opal PUBLIC ${OPAL_SRTP_LINK})
endif()
if(OPAL_AEC_LINK)
  target_link_libraries(opal PUBLIC ${OPAL_AEC_LINK})
endif()
if(CMAKE_DL_LIBS)
  target_link_libraries(opal PUBLIC ${CMAKE_DL_LIBS})
endif()

if(OPAL_PLUGINS)
  add_subdirectory(plugins)
endif()
if(OPAL_BUILD_SAMPLES)
  add_subdirectory(samples)
endif()

install(TARGETS opal
  EXPORT OPALTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${OPAL_SOURCE_DIR}/include/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.inl")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/opal_config.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

file(READ "${OPAL_SOURCE_DIR}/opal.pc.in" _opal_pc)
string(REGEX REPLACE "\r?\nmakedir=@makedir@\r?\n" "\n" _opal_pc "${_opal_pc}")
string(REPLACE "@OPAL_VERSION@" "${OPAL_VERSION}" _opal_pc "${_opal_pc}")
string(REPLACE "@prefix@" "${CMAKE_INSTALL_PREFIX}" _opal_pc "${_opal_pc}")
string(REPLACE "@exec_prefix@" "${CMAKE_INSTALL_PREFIX}" _opal_pc "${_opal_pc}")
string(REPLACE "@libdir@" "${CMAKE_INSTALL_FULL_LIBDIR}" _opal_pc "${_opal_pc}")
string(REPLACE "@includedir@" "${CMAKE_INSTALL_FULL_INCLUDEDIR}" _opal_pc "${_opal_pc}")
string(REPLACE "@datarootdir@" "${CMAKE_INSTALL_FULL_DATAROOTDIR}" _opal_pc "${_opal_pc}")
string(REPLACE "@OPAL_PLUGIN_DIR@" "${OPAL_PLUGIN_DIR}" _opal_pc "${_opal_pc}")
string(REPLACE "@target_cpu@" "${CMAKE_SYSTEM_PROCESSOR}" _opal_pc "${_opal_pc}")
string(REPLACE "@target_os@" "${CMAKE_SYSTEM_NAME}" _opal_pc "${_opal_pc}")
string(REPLACE "@target@" "${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}" _opal_pc "${_opal_pc}")
string(REPLACE "@CPPFLAGS@" "" _opal_pc "${_opal_pc}")
string(REPLACE "@CFLAGS@" "" _opal_pc "${_opal_pc}")
string(REPLACE "@CXXFLAGS@" "" _opal_pc "${_opal_pc}")
string(REPLACE "@LDFLAGS@" "" _opal_pc "${_opal_pc}")
string(REPLACE "@LIBS@" "" _opal_pc "${_opal_pc}")

macro(opal_pc_set name enabled)
  set(_pc_value no)
  if(${enabled})
    set(_pc_value yes)
  endif()
  string(REPLACE "@${name}@" "${_pc_value}" _opal_pc "${_opal_pc}")
endmacro()
opal_pc_set(OPAL_H323 OPAL_H323)
opal_pc_set(OPAL_SIP OPAL_SIP)
opal_pc_set(OPAL_IAX2 FALSE)
opal_pc_set(OPAL_VIDEO OPAL_VIDEO)
opal_pc_set(OPAL_BFCP OPAL_BFCP)
opal_pc_set(OPAL_ZRTP FALSE)
opal_pc_set(OPAL_LID OPAL_LID)
opal_pc_set(OPAL_IVR OPAL_IVR)
opal_pc_set(OPAL_HAS_H224 OPAL_HAS_H224)
opal_pc_set(OPAL_HAS_H281 OPAL_HAS_H281)
opal_pc_set(OPAL_H460 OPAL_H460)
opal_pc_set(OPAL_SRTP OPAL_SRTP_ENABLED)
opal_pc_set(OPAL_RFC4175 OPAL_RFC4175)
opal_pc_set(OPAL_AEC OPAL_AEC)
opal_pc_set(OPAL_G711PLC OPAL_G711PLC)
opal_pc_set(OPAL_T38_CAPABILITY OPAL_T38_CAPABILITY)
opal_pc_set(OPAL_FAX OPAL_FAX)
opal_pc_set(OPAL_JAVA OPAL_JAVA)
opal_pc_set(OPAL_CSHARP OPAL_CSHARP)
opal_pc_set(OPAL_HAS_MSRP OPAL_HAS_MSRP)
opal_pc_set(OPAL_HAS_SIPIM OPAL_HAS_SIPIM)
opal_pc_set(OPAL_HAS_RFC4103 OPAL_HAS_RFC4103)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/opal.pc" "${_opal_pc}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/opal.pc"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OPALConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/OPALConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OPAL")
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/OPALConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/OPALConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/OPALConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OPAL")
install(EXPORT OPALTargets
  NAMESPACE OPAL::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OPAL")
# Build-tree package so a consumer can find_package(OPAL) without installing.
export(EXPORT OPALTargets
  FILE "${CMAKE_CURRENT_BINARY_DIR}/OPALTargets.cmake"
  NAMESPACE OPAL::)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  @ONLY)
add_custom_target(uninstall
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  COMMENT "Removing the files installed from this build"
  VERBATIM)

include(cmake/OPALBuildVariants.cmake)
