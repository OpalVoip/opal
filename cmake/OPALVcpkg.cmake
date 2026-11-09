# Loaded before project(). On Windows, select a vcpkg toolchain when the
# caller did not pass one, then install every codec port that builds for
# the Visual Studio triplet.

if(CMAKE_HOST_WIN32 AND NOT DEFINED OPAL_USE_VCPKG)
  set(OPAL_USE_VCPKG ON CACHE BOOL
    "On Windows, use vcpkg for dependencies when a checkout is found")
endif()

function(opal_find_vcpkg out)
  set(_found "")
  foreach(_root IN ITEMS "$ENV{VCPKG_ROOT}" "$ENV{VCPKG_INSTALLATION_ROOT}")
    if(_root AND EXISTS "${_root}/scripts/buildsystems/vcpkg.cmake")
      set(_found "${_root}/scripts/buildsystems/vcpkg.cmake")
      break()
    endif()
  endforeach()
  if(NOT _found)
    find_program(OPAL_VCPKG_EXECUTABLE NAMES vcpkg vcpkg.exe)
    if(OPAL_VCPKG_EXECUTABLE)
      get_filename_component(_root "${OPAL_VCPKG_EXECUTABLE}" DIRECTORY)
      if(EXISTS "${_root}/scripts/buildsystems/vcpkg.cmake")
        set(_found "${_root}/scripts/buildsystems/vcpkg.cmake")
      endif()
    endif()
  endif()
  if(NOT _found)
    set(_vswhere "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer/vswhere.exe")
    if(NOT EXISTS "${_vswhere}")
      set(_vswhere "$ENV{ProgramFiles}/Microsoft Visual Studio/Installer/vswhere.exe")
    endif()
    if(EXISTS "${_vswhere}")
      execute_process(
        COMMAND "${_vswhere}" -latest -products * -requires Microsoft.VisualStudio.Component.Vcpkg
                -property installationPath
        OUTPUT_VARIABLE _vs
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
      string(STRIP "${_vs}" _vs)
      if(_vs AND EXISTS "${_vs}/VC/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(_found "${_vs}/VC/vcpkg/scripts/buildsystems/vcpkg.cmake")
      endif()
    endif()
  endif()
  if(NOT _found)
    foreach(_root IN ITEMS
        "$ENV{USERPROFILE}/vcpkg"
        "$ENV{USERPROFILE}/source/vcpkg"
        "$ENV{LOCALAPPDATA}/vcpkg"
        "C:/vcpkg"
        "C:/src/vcpkg")
      if(_root AND EXISTS "${_root}/scripts/buildsystems/vcpkg.cmake")
        set(_found "${_root}/scripts/buildsystems/vcpkg.cmake")
        break()
      endif()
    endforeach()
  endif()
  if(_found)
    file(TO_CMAKE_PATH "${_found}" _found)
  endif()
  set(${out} "${_found}" PARENT_SCOPE)
endfunction()

if(CMAKE_HOST_WIN32 AND OPAL_USE_VCPKG AND NOT DEFINED CACHE{CMAKE_TOOLCHAIN_FILE} AND NOT CMAKE_TOOLCHAIN_FILE)
  opal_find_vcpkg(_opal_vcpkg)
  if(_opal_vcpkg)
    set(CMAKE_TOOLCHAIN_FILE "${_opal_vcpkg}" CACHE FILEPATH "vcpkg toolchain")
    message(STATUS "OPAL: using vcpkg toolchain ${_opal_vcpkg}")
  else()
    message(STATUS "OPAL: vcpkg was not found; Windows dependencies are not taken from vcpkg")
  endif()
  unset(_opal_vcpkg)
endif()

# Called before project(). An explicit VCPKG_MANIFEST_FEATURES list,
# including an empty one, is left alone. wxwidgets is added separately
# when the samples are built.
function(opal_vcpkg_windows_features)
  if(NOT CMAKE_HOST_WIN32 OR NOT OPAL_USE_VCPKG)
    return()
  endif()
  if(NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg\\.cmake$")
    return()
  endif()
  if(DEFINED CACHE{VCPKG_MANIFEST_FEATURES} OR DEFINED ENV{VCPKG_MANIFEST_FEATURES})
    return()
  endif()
  if(VCPKG_MANIFEST_FEATURES)
    return()
  endif()
  # CELT and SpanDSP are not in the vcpkg registry. Speex and Theora are
  # obsolete codecs and are installed only when that flag is on.
  set(_features opus vpx openh264)
  if(DEFINED OPAL_OBSOLETE_CODECS AND OPAL_OBSOLETE_CODECS AND NOT OPAL_OPALMIN)
    list(APPEND _features speex theora)
  endif()
  set(VCPKG_MANIFEST_FEATURES ${_features} PARENT_SCOPE)
  string(REPLACE ";" ", " _features "${_features}")
  message(STATUS "OPAL: vcpkg features ${_features}")
endfunction()
