function(opal_sample target)
  add_executable(${target} ${ARGN})
  target_link_libraries(${target} PRIVATE opal)
endfunction()

opal_sample(c_api c_api/main.c)
if(OPAL_HAVE_PTLIB_HTTPSVC)
  opal_sample(opalsrv
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
find_program(OPAL_WX_CONFIG NAMES wx-config
  HINTS /opt/homebrew/bin /usr/local/bin /opt/local/bin)
if(OPAL_WX_CONFIG)
  set(wxWidgets_CONFIG_EXECUTABLE "${OPAL_WX_CONFIG}")
  find_package(wxWidgets QUIET COMPONENTS core base adv xrc html xml net)
endif()
if(wxWidgets_FOUND)
  find_program(OPAL_WXRC NAMES wxrc
    HINTS "${wxWidgets_ROOT_DIR}/bin" /opt/homebrew/bin /usr/local/bin /opt/local/bin)
  if(NOT OPAL_WXRC)
    get_filename_component(_wx_bin "${OPAL_WX_CONFIG}" DIRECTORY)
    find_program(OPAL_WXRC NAMES wxrc HINTS "${_wx_bin}")
  endif()
endif()
if(wxWidgets_FOUND AND OPAL_WXRC)
  set(_openphone_xrc_cxx "${CMAKE_CURRENT_BINARY_DIR}/openphone.cxx")
  add_custom_command(
    OUTPUT "${_openphone_xrc_cxx}"
    COMMAND "${OPAL_WXRC}" "${CMAKE_CURRENT_SOURCE_DIR}/openphone/openphone.xrc"
            -c -o "${_openphone_xrc_cxx}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/openphone/openphone.xrc"
    VERBATIM)
  add_executable(openphone openphone/main.cxx "${_openphone_xrc_cxx}")
  target_include_directories(openphone PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/openphone"
    ${wxWidgets_INCLUDE_DIRS})
  target_compile_definitions(openphone PRIVATE ${wxWidgets_DEFINITIONS})
  separate_arguments(_wx_cxx_flags NATIVE_COMMAND "${wxWidgets_CXX_FLAGS}")
  target_compile_options(openphone PRIVATE ${_wx_cxx_flags})
  target_link_libraries(openphone PRIVATE opal ${wxWidgets_LIBRARIES})
  message(STATUS "OpenPhone will be built with wxWidgets ${wxWidgets_VERSION_STRING}")
else()
  message(STATUS "OpenPhone skipped; wx-config and wxrc were not found")
endif()
