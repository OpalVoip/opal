function(opal_sample target)
  cmake_parse_arguments(OPAL_SAMPLE "WIN32" "" "" ${ARGN})
  if(OPAL_SAMPLE_WIN32)
    add_executable(${target} WIN32 ${OPAL_SAMPLE_UNPARSED_ARGUMENTS})
  else()
    add_executable(${target} ${OPAL_SAMPLE_UNPARSED_ARGUMENTS})
  endif()
  target_link_libraries(${target} PRIVATE opal)
endfunction()

opal_sample(c_api c_api/main.c)
if(OPAL_HAVE_PTLIB_HTTPSVC)
  # PHTTPServiceProcess uses WinMain on Windows (see PCREATE_PROCESS in svcproc.h).
  opal_sample(opalsrv WIN32
    server/main.cxx server/custom.cxx server/h323serv.cxx
    server/sipserv.cxx server/status.cxx server/cdr.cxx)
endif()
if(OPAL_HAS_PCSS)
  opal_sample(console console/main.cxx)
endif()
if(OPAL_VIDEO)
  opal_sample(codectest codectest/main.cxx)
endif()
if(OPAL_HAS_PCSS AND OPAL_IVR AND OPAL_HAVE_PTLIB_CLI)
  opal_sample(callgen callgen/main.cxx)
endif()
if(OPAL_SIP AND OPAL_HAS_PRESENCE)
  opal_sample(presence test/presence/main.cxx)
endif()
if(OPAL_SDP)
  opal_sample(icetest test/ice/main.cxx)
endif()
if(OPAL_HAVE_PTLIB_CLI AND OPAL_FAX)
  opal_sample(fax fax/main.cxx)
endif()
if(OPAL_HAVE_PTLIB_CLI AND OPAL_IVR)
  opal_sample(ivr ivr/main.cxx)
endif()
if(OPAL_HAVE_PTLIB_CLI AND OPAL_IVR AND OPAL_HAS_MIXER)
  opal_sample(mcu mcu/main.cxx)
endif()
if(OPAL_HAVE_PTLIB_GSTREAMER)
  opal_sample(gstreamer gstreamer/main.cxx)
endif()

# OpenPhone needs wxWidgets and the wxrc compiler for openphone.xrc.
# Unix installs expose both through wx-config. A Windows source tree is the
# directory in WXDIR, which is the same variable the Visual Studio projects use.
# vcpkg's toolchain wrapper finds the libraries itself and installs wxrc in
# tools/wxwidgets, so WXDIR must not hide that root.
find_program(OPAL_WX_CONFIG NAMES wx-config
  HINTS /opt/homebrew/bin /usr/local/bin /opt/local/bin)
if(OPAL_WX_CONFIG AND NOT VCPKG_TOOLCHAIN)
  set(wxWidgets_CONFIG_EXECUTABLE "${OPAL_WX_CONFIG}")
endif()
if(WIN32 AND NOT wxWidgets_ROOT_DIR AND DEFINED ENV{WXDIR} AND NOT VCPKG_TOOLCHAIN)
  set(wxWidgets_ROOT_DIR "$ENV{WXDIR}")
endif()
find_package(wxWidgets QUIET COMPONENTS core base adv xrc html xml net)
if(wxWidgets_FOUND)
  set(_wxrc_hints
    "${wxWidgets_ROOT_DIR}/tools/wxwidgets"
    "${wxWidgets_ROOT_DIR}/bin"
    "$ENV{WXDIR}/tools/wxwidgets"
    "$ENV{WXDIR}/bin"
    /opt/homebrew/bin /usr/local/bin /opt/local/bin)
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND _wxrc_hints
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/wxwidgets")
  endif()
  # A previous configure caches a failed search and will not try the new hints.
  if(NOT OPAL_WXRC)
    unset(OPAL_WXRC CACHE)
  endif()
  find_program(OPAL_WXRC NAMES wxrc wxrc.exe HINTS ${_wxrc_hints})
  if(NOT OPAL_WXRC AND OPAL_WX_CONFIG)
    get_filename_component(_wx_bin "${OPAL_WX_CONFIG}" DIRECTORY)
    find_program(OPAL_WXRC NAMES wxrc wxrc.exe HINTS "${_wx_bin}")
  endif()
  unset(_wxrc_hints)
endif()
if(wxWidgets_FOUND AND OPAL_WXRC)
  set(_openphone_xrc_cxx "${CMAKE_CURRENT_BINARY_DIR}/openphone.cxx")
  add_custom_command(
    OUTPUT "${_openphone_xrc_cxx}"
    COMMAND "${OPAL_WXRC}" "${CMAKE_CURRENT_SOURCE_DIR}/openphone/openphone.xrc"
            -c -o "${_openphone_xrc_cxx}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/openphone/openphone.xrc"
    VERBATIM)
  add_executable(openphone WIN32 openphone/main.cxx "${_openphone_xrc_cxx}")
  target_include_directories(openphone PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/openphone"
    ${wxWidgets_INCLUDE_DIRS})
  target_compile_definitions(openphone PRIVATE ${wxWidgets_DEFINITIONS})
  separate_arguments(_wx_cxx_flags NATIVE_COMMAND "${wxWidgets_CXX_FLAGS}")
  target_compile_options(openphone PRIVATE ${_wx_cxx_flags})
  target_link_libraries(openphone PRIVATE opal ${wxWidgets_LIBRARIES})
  message(STATUS "OpenPhone will be built with wxWidgets ${wxWidgets_VERSION_STRING}")
elseif(NOT wxWidgets_FOUND)
  message(STATUS "OpenPhone skipped; wxWidgets was not found")
else()
  message(STATUS "OpenPhone skipped; wxrc was not found")
endif()
