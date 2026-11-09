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
      set_target_properties("${_target}" PROPERTIES
        VS_USER_PROPS "${_opal_vs_user_props}")
    endif()
  endforeach()
  get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(_subdir IN LISTS _subdirs)
    opal_attach_vs_props("${_subdir}")
  endforeach()
endfunction()

opal_attach_vs_props("${CMAKE_SOURCE_DIR}")
