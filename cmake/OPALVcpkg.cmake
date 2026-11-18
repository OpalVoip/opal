# Loaded before project(). On Windows, select a vcpkg toolchain when the
# caller did not pass one, then install every codec port that builds for
# the Visual Studio triplet.

if(CMAKE_HOST_WIN32 AND NOT DEFINED OPAL_USE_VCPKG)
  set(OPAL_USE_VCPKG ON CACHE BOOL
    "On Windows, use vcpkg for dependencies when a checkout is found")
endif()

# An unset Visual Studio platform is filled in after the toolchain loads.
# vcpkg then guesses x86-windows, and ports such as krb5 refuse that triplet.
# -A Win32 and an explicit triplet are left alone.
if(CMAKE_HOST_WIN32
    AND CMAKE_GENERATOR MATCHES "^Visual Studio"
    AND NOT VCPKG_TARGET_TRIPLET
    AND NOT DEFINED ENV{VCPKG_TARGET_TRIPLET}
    AND NOT CMAKE_GENERATOR_PLATFORM MATCHES "^[Ww][Ii][Nn]32$")
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
    set(_opal_triplet arm64-windows)
  else()
    set(_opal_triplet x64-windows)
  endif()
  set(VCPKG_TARGET_TRIPLET "${_opal_triplet}" CACHE STRING "vcpkg triplet")
  message(STATUS "OPAL: vcpkg triplet ${VCPKG_TARGET_TRIPLET}")
  unset(_opal_triplet)
endif()

# The manifest baseline's ports call vcpkg_host_path_list(REMOVE_DUPLICATES).
# Visual Studio 2022's bundled scripts do not implement that operation, so
# expat fails with "Operation REMOVE_DUPLICATES not recognized."
function(opal_vcpkg_root_current root out)
  set(${out} FALSE PARENT_SCOPE)
  set(_list "${root}/scripts/cmake/vcpkg_host_path_list.cmake")
  if(NOT EXISTS "${_list}")
    return()
  endif()
  file(READ "${_list}" _text)
  if(_text MATCHES "REMOVE_DUPLICATES")
    set(${out} TRUE PARENT_SCOPE)
  endif()
endfunction()

function(opal_find_vcpkg out)
  set(_roots "")
  if(NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    list(APPEND _roots "$ENV{VCPKG_ROOT}")
  endif()
  if(NOT "$ENV{VCPKG_INSTALLATION_ROOT}" STREQUAL "")
    list(APPEND _roots "$ENV{VCPKG_INSTALLATION_ROOT}")
  endif()
  find_program(OPAL_VCPKG_EXECUTABLE NAMES vcpkg vcpkg.exe)
  if(OPAL_VCPKG_EXECUTABLE)
    get_filename_component(_root "${OPAL_VCPKG_EXECUTABLE}" DIRECTORY)
    list(APPEND _roots "${_root}")
  endif()
  set(_vswhere "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer/vswhere.exe")
  if(NOT EXISTS "${_vswhere}")
    set(_vswhere "$ENV{ProgramFiles}/Microsoft Visual Studio/Installer/vswhere.exe")
  endif()
  if(EXISTS "${_vswhere}")
    # Newest Visual Studio first. Require the C++ tools, not the Vcpkg
    # component: the 2022 component is older than the manifest baseline.
    execute_process(
      COMMAND "${_vswhere}" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
              -sort -property installationPath
      OUTPUT_VARIABLE _vs_list
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    string(REGEX REPLACE "\r?\n" ";" _vs_list "${_vs_list}")
    foreach(_vs IN LISTS _vs_list)
      string(STRIP "${_vs}" _vs)
      if(_vs)
        list(APPEND _roots "${_vs}/VC/vcpkg")
      endif()
    endforeach()
  endif()
  list(APPEND _roots
    "$ENV{USERPROFILE}/vcpkg"
    "$ENV{USERPROFILE}/source/vcpkg"
    "$ENV{LOCALAPPDATA}/vcpkg"
    "C:/vcpkg"
    "C:/src/vcpkg")

  set(_found "")
  set(_stale "")
  foreach(_root IN LISTS _roots)
    if(NOT _root OR NOT EXISTS "${_root}/scripts/buildsystems/vcpkg.cmake")
      continue()
    endif()
    file(TO_CMAKE_PATH "${_root}" _root)
    opal_vcpkg_root_current("${_root}" _current)
    if(_current)
      set(_found "${_root}/scripts/buildsystems/vcpkg.cmake")
      break()
    elseif(NOT _stale)
      set(_stale "${_root}")
    endif()
  endforeach()
  if(_found AND _stale)
    message(STATUS "OPAL: skipping vcpkg at ${_stale}; its scripts are older than the manifest baseline")
  elseif(NOT _found AND _stale)
    message(FATAL_ERROR
      "vcpkg at ${_stale} is older than the manifest baseline in vcpkg.json.\n"
      "Its scripts do not implement REMOVE_DUPLICATES, so building expat stops with "
      "\"Operation REMOVE_DUPLICATES not recognized.\"\n"
      "Install the vcpkg component in Visual Studio 2026, or clone "
      "https://github.com/microsoft/vcpkg and set VCPKG_ROOT to that directory.")
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
