# The hand-written vcxproj files import OPAL.props after the per-user sheet.
# That sheet supplies LocalDebuggerEnvironment, so Visual Studio can find the
# DLLs a program needs when it starts under the debugger. CMake has one slot
# for that import (VS_USER_PROPS), so a wrapper keeps the user sheet and then
# imports OPAL.props.

if(NOT CMAKE_GENERATOR MATCHES "^Visual Studio")
  return()
endif()

file(TO_CMAKE_PATH "${OPAL_SOURCE_DIR}/src/win32/OPAL.props" _opal_sheet)
set(_opal_vs_user_props "${CMAKE_BINARY_DIR}/OPAL.user.props")
file(WRITE "${_opal_vs_user_props}"
"<?xml version=\"1.0\" encoding=\"utf-8\"?>
<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">
  <Import Project=\"\$(UserRootDir)\\Microsoft.Cpp.\$(Platform).user.props\" Condition=\"exists('\$(UserRootDir)\\Microsoft.Cpp.\$(Platform).user.props')\" />
  <Import Project=\"${_opal_sheet}\" />
</Project>
")

function(opal_attach_vs_props dir)
  get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_target IN LISTS _targets)
    get_target_property(_aliased "${_target}" ALIASED_TARGET)
    if(_aliased)
      continue()
    endif()
    get_target_property(_imported "${_target}" IMPORTED)
    if(_imported)
      continue()
    endif()
    get_target_property(_type "${_target}" TYPE)
    if(_type MATCHES "^(EXECUTABLE|SHARED_LIBRARY|STATIC_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
      # Release and Debug must not share .obj or compiler pdb files. A
      # combined build locks the file and the other configuration fails.
      set_target_properties("${_target}" PROPERTIES
        VS_USER_PROPS "${_opal_vs_user_props}"
        VS_GLOBAL_IntDir "${CMAKE_BINARY_DIR}/obj/$(Platform)/$(Configuration)/$(ProjectName)/")
    endif()
  endforeach()
  get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(_subdir IN LISTS _subdirs)
    opal_attach_vs_props("${_subdir}")
  endforeach()
endfunction()

opal_attach_vs_props("${CMAKE_SOURCE_DIR}")

# List the projects from a PTLib Visual Studio build in this solution, so
# both trees can be edited without switching solutions. The projects keep
# building in the PTLib build directory.
set(OPAL_INCLUDE_PTLIB_PROJECTS ON CACHE BOOL
  "Add PTLib Visual Studio projects to this solution when that build has them")
if(NOT OPAL_INCLUDE_PTLIB_PROJECTS OR NOT PTLib_DIR)
  return()
endif()
function(opal_depend_on_external_ptlib dir ptlib_target)
  get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_t IN LISTS _targets)
    if(_t STREQUAL "${ptlib_target}")
      continue()
    endif()
    get_target_property(_libs "${_t}" LINK_LIBRARIES)
    if(_libs AND "PTLib::ptlib" IN_LIST _libs)
      add_dependencies("${_t}" "${ptlib_target}")
    endif()
  endforeach()
  get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(_subdir IN LISTS _subdirs)
    opal_depend_on_external_ptlib("${_subdir}" "${ptlib_target}")
  endforeach()
endfunction()

file(GLOB_RECURSE _ptlib_vcxproj "${PTLib_DIR}/*.vcxproj")
set(_ptlib_skip ALL_BUILD ZERO_CHECK INSTALL RUN_TESTS PACKAGE Nightly Experimental Continuous
  debug both uninstall)
set(_ptlib_added 0)
foreach(_proj IN LISTS _ptlib_vcxproj)
  file(TO_CMAKE_PATH "${_proj}" _proj)
  if(_proj MATCHES "/CMakeFiles/")
    continue()
  endif()
  # A nested configure, such as build/debug, repeats the same project names.
  get_filename_component(_ptlib_root "${PTLib_DIR}" ABSOLUTE)
  file(TO_CMAKE_PATH "${_ptlib_root}" _ptlib_root)
  get_filename_component(_walk "${_proj}" DIRECTORY)
  set(_nested FALSE)
  while(NOT _walk STREQUAL "${_ptlib_root}")
    if(EXISTS "${_walk}/CMakeCache.txt")
      set(_nested TRUE)
      break()
    endif()
    get_filename_component(_next "${_walk}" DIRECTORY)
    if(_next STREQUAL "${_walk}")
      break()
    endif()
    set(_walk "${_next}")
  endwhile()
  if(_nested)
    continue()
  endif()
  get_filename_component(_name "${_proj}" NAME_WE)
  if(_name IN_LIST _ptlib_skip)
    continue()
  endif()
  set(_target "${_name}")
  if(TARGET "${_target}")
    set(_target "ptlib_${_name}")
  endif()
  if(TARGET "${_target}")
    continue()
  endif()
  file(READ "${_proj}" _xml)
  set(_guid_args "")
  string(REGEX MATCH "<ProjectGuid>[{]([0-9A-Fa-f-]+)[}]</ProjectGuid>" _guid "${_xml}")
  if(_guid)
    set(_guid_args GUID "{${CMAKE_MATCH_1}}")
  endif()
  set(_platform_args "")
  if(CMAKE_GENERATOR_PLATFORM)
    set(_platform_args PLATFORM "${CMAKE_GENERATOR_PLATFORM}")
  endif()
  include_external_msproject(${_target} "${_proj}"
    ${_guid_args} ${_platform_args})
  set_target_properties(${_target} PROPERTIES FOLDER "PTLib")
  if(_name STREQUAL "ptlib")
    # Plugins link ptlib64.lib. They have to wait until that project has
    # produced it, or the link runs against a file that is not there yet.
    if(TARGET opal)
      add_dependencies(opal ${_target})
    endif()
    opal_depend_on_external_ptlib("${CMAKE_SOURCE_DIR}" "${_target}")
  endif()
  math(EXPR _ptlib_added "${_ptlib_added} + 1")
endforeach()
if(_ptlib_added EQUAL 0)
  message(STATUS
    "OPAL: no Visual Studio projects in ${PTLib_DIR}. Configure PTLib with the same generator to list them here.")
else()
  message(STATUS "OPAL: added ${_ptlib_added} PTLib projects from ${PTLib_DIR}")
endif()
