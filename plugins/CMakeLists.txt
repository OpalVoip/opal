function(opal_add_plugin name family)
  set(target ${name}_ptplugin)
  add_library(${target} MODULE ${ARGN})
  target_compile_definitions(${target} PRIVATE PLUGIN_CODEC_DLL_EXPORTS)
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${OPAL_SOURCE_DIR}/include"
    "${CMAKE_BINARY_DIR}/include")
  target_link_libraries(${target} PRIVATE PTLib::ptlib)
  set_target_properties(${target} PROPERTIES
    PREFIX ""
    OUTPUT_NAME ${name}_ptplugin
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${family}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${family}")
  if(APPLE)
    set_target_properties(${target} PROPERTIES SUFFIX ".dylib")
  elseif(WIN32 OR CYGWIN)
    set_target_properties(${target} PROPERTIES SUFFIX ".dll")
  else()
    set_target_properties(${target} PROPERTIES SUFFIX ".so")
  endif()
  install(TARGETS ${target}
    LIBRARY DESTINATION "${OPAL_PLUGIN_INSTALL_DIR}/${family}"
    RUNTIME DESTINATION "${OPAL_PLUGIN_INSTALL_DIR}/${family}")
endfunction()

set(OPAL_PLUGIN_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/opal-${OPAL_VERSION}")
if(WIN32 AND (MINGW OR CYGWIN))
  set(OPAL_PLUGIN_INSTALL_DIR "plugins")
endif()

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/plugin_config.h"
"#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
")
if(NOT WIN32)
  file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/plugin_config.h" "#define HAVE_UNISTD_H 1\n")
endif()

opal_add_plugin(g726 audio
  audio/G726/g726codec.c
  audio/G726/g711.c
  audio/G726/g726/g726_16.c
  audio/G726/g726/g726_24.c
  audio/G726/g726/g726_32.c
  audio/G726/g726/g726_40.c
  audio/G726/g726/g72x.c)
target_include_directories(g726_ptplugin PRIVATE audio/G726)

opal_add_plugin(g722 audio
  audio/G722/g722codec.c
  audio/G722/VoIPCodecs/g722_encode.c
  audio/G722/VoIPCodecs/g722_decode.c
  audio/G722/VoIPCodecs/bitstream.c)
target_include_directories(g722_ptplugin PRIVATE audio/G722 audio/G722/VoIPCodecs)

opal_add_plugin(ima_adpcm audio audio/IMA_ADPCM/ima_adpcm.c)

file(GLOB _lpc10 audio/LPC_10/src/*.c)
opal_add_plugin(lpc10 audio audio/LPC_10/lpc_10_codec.c ${_lpc10})
target_include_directories(lpc10_ptplugin PRIVATE audio/LPC_10 audio/LPC_10/src)
# Vendored f2c sources contain nested block comments.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(lpc10_ptplugin PRIVATE -Wno-comment)
endif()
# f2c.h #defines abs(). A system malloc.h that includes stdlib.h then fails
# to declare abs(). This header declares the allocators and nothing else.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/compat/malloc.h"
"#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
")
target_include_directories(lpc10_ptplugin BEFORE PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/compat")

opal_add_plugin(gsm0610 audio
  audio/GSM0610/gsm06_10_codec.c
  audio/GSM0610/src/gsm_create.c
  audio/GSM0610/src/gsm_destroy.c
  audio/GSM0610/src/gsm_decode.c
  audio/GSM0610/src/gsm_encode.c
  audio/GSM0610/src/gsm_option.c
  audio/GSM0610/src/code.c
  audio/GSM0610/src/decode.c
  audio/GSM0610/src/add.c
  audio/GSM0610/src/gsm_lpc.c
  audio/GSM0610/src/rpe.c
  audio/GSM0610/src/preprocess.c
  audio/GSM0610/src/long_term.c
  audio/GSM0610/src/short_term.c
  audio/GSM0610/src/table.c)
target_include_directories(gsm0610_ptplugin PRIVATE audio/GSM0610/inc)
target_compile_definitions(gsm0610_ptplugin PRIVATE WAV49 NeedFunctionPrototypes=1)

opal_add_plugin(h261_vic video
  video/H.261-vic/h261vic.cxx
  video/H.261-vic/vic/encoder-h261.cxx
  video/H.261-vic/vic/transmitter.cxx
  video/H.261-vic/vic/vid_coder.cxx
  video/H.261-vic/vic/p64encoder.cxx
  video/H.261-vic/vic/p64.cxx
  video/H.261-vic/vic/dct.cxx
  video/H.261-vic/vic/huffcode.c
  video/H.261-vic/vic/bv.c)
target_include_directories(h261_vic_ptplugin PRIVATE video/common video/H.261-vic/vic)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(h261_vic_ptplugin PRIVATE -Wno-register)
endif()

find_package(PkgConfig QUIET)

# Homebrew static archives are "current ar archive random library": Mach-O
# objects, which the MinGW linker cannot read.
function(opal_path_is_host_binary path result)
  set(${result} FALSE PARENT_SCOPE)
  if(NOT path OR NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
    return()
  endif()
  if(path MATCHES "\\.(dylib|so)(\\.[0-9.]+)?$")
    set(${result} TRUE PARENT_SCOPE)
    return()
  endif()
  find_program(OPAL_FILE_COMMAND file)
  if(NOT OPAL_FILE_COMMAND)
    return()
  endif()
  execute_process(
    COMMAND "${OPAL_FILE_COMMAND}" -b "${path}"
    OUTPUT_VARIABLE _kind
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(_kind MATCHES "Mach-O|random library")
    set(${result} TRUE PARENT_SCOPE)
  endif()
endfunction()

function(opal_pkg_is_target_library prefix result)
  set(_ok TRUE)
  if(WIN32)
    if("${${prefix}_LDFLAGS}" MATCHES "-framework")
      set(_ok FALSE)
    endif()
    set(_files "")
    foreach(_lib IN LISTS ${prefix}_LINK_LIBRARIES ${prefix}_LIBRARIES)
      if(EXISTS "${_lib}" AND NOT IS_DIRECTORY "${_lib}")
        list(APPEND _files "${_lib}")
      endif()
    endforeach()
    foreach(_dir IN LISTS ${prefix}_LIBRARY_DIRS)
      foreach(_name IN LISTS ${prefix}_LIBRARIES)
        foreach(_cand IN ITEMS
            "${_dir}/lib${_name}.a"
            "${_dir}/lib${_name}.dll.a"
            "${_dir}/lib${_name}.dylib"
            "${_dir}/lib${_name}.so"
            "${_dir}/${_name}")
          if(EXISTS "${_cand}")
            list(APPEND _files "${_cand}")
          endif()
        endforeach()
      endforeach()
    endforeach()
    if(TARGET PkgConfig::${prefix})
      foreach(_prop IN ITEMS IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_DEBUG)
        get_target_property(_loc "PkgConfig::${prefix}" "${_prop}")
        if(_loc)
          list(APPEND _files "${_loc}")
        endif()
      endforeach()
      get_target_property(_iface "PkgConfig::${prefix}" INTERFACE_LINK_LIBRARIES)
      if(_iface)
        foreach(_dep IN LISTS _iface)
          if(EXISTS "${_dep}" AND NOT IS_DIRECTORY "${_dep}")
            list(APPEND _files "${_dep}")
          endif()
        endforeach()
      endif()
    endif()
    foreach(_file IN LISTS _files)
      opal_path_is_host_binary("${_file}" _host)
      if(_host)
        set(_ok FALSE)
      endif()
    endforeach()
  endif()
  set(${result} "${_ok}" PARENT_SCOPE)
endfunction()

# Probe a pkg-config module and publish both the FOUND flag and PkgConfig::<prefix>.
function(opal_try_pkg prefix)
  if(NOT PKG_CONFIG_FOUND)
    set(${prefix}_FOUND FALSE PARENT_SCOPE)
    return()
  endif()
  pkg_check_modules(${prefix} IMPORTED_TARGET ${ARGN})
  set(_found "${${prefix}_FOUND}")
  if(_found AND WIN32)
    opal_pkg_is_target_library(${prefix} _usable)
    if(NOT _usable)
      message(STATUS "OPAL: ignoring host pkg-config result for ${ARGN}; not a Windows library")
      set(_found FALSE)
      set(${prefix}_FOUND FALSE CACHE INTERNAL "" FORCE)
    endif()
  endif()
  set(${prefix}_FOUND "${_found}" PARENT_SCOPE)
endfunction()

function(opal_link_pkg target prefix)
  if(TARGET PkgConfig::${prefix})
    target_link_libraries(${target} PRIVATE PkgConfig::${prefix})
  endif()
endfunction()

opal_try_pkg(OPAL_PC_OPUS opus)
opal_try_pkg(OPAL_PC_SPEEX speex)
opal_try_pkg(OPAL_PC_AVCODEC libavcodec libavutil)
opal_try_pkg(OPAL_PC_THEORA theoraenc theoradec ogg)
opal_try_pkg(OPAL_PC_VPX vpx)
opal_try_pkg(OPAL_PC_X264 x264)
opal_try_pkg(OPAL_PC_CELT celt)
opal_try_pkg(OPAL_PC_SPANDSP spandsp)
opal_try_pkg(OPAL_PC_OPENH264 openh264)

if(UNIX)
  set(OPAL_PLUGIN_LIBM m)
endif()

# --- Audio codecs with sources in the tree ---

file(GLOB _g7221 audio/G.722.1/libg722_1/src/*.c)
list(FILTER _g7221 EXCLUDE REGEX "/make_")
opal_add_plugin(g7221 audio audio/G.722.1/G7221Codec.c ${_g7221})
target_include_directories(g7221_ptplugin PRIVATE
  audio/G.722.1/libg722_1/src
  audio/G.722.1/libg722_1/src/generated)
target_link_libraries(g7221_ptplugin PRIVATE ${OPAL_PLUGIN_LIBM})

file(GLOB _g7222 audio/G.722.2/AMR-WB/*.c)
opal_add_plugin(g7222 audio audio/G.722.2/G7222Codec.cpp ${_g7222})
target_include_directories(g7222_ptplugin PRIVATE audio/G.722.2 audio/G.722.2/AMR-WB)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/audio/SBC/sbc.h" _sbc_header)
if(_sbc_header MATCHES "#ifndef|#define|#include")
  opal_add_plugin(sbc audio audio/SBC/sbccodec.cxx audio/SBC/sbc.c)
  target_include_directories(sbc_ptplugin PRIVATE audio/SBC)
  target_link_libraries(sbc_ptplugin PRIVATE ${OPAL_PLUGIN_LIBM})
else()
  message(STATUS "OPAL audio plugin sbc skipped; the bluez SBC sources are not in the tree")
endif()

file(GLOB _silk audio/SILK/SILK_SDK/src/*.c)
opal_add_plugin(silk audio audio/SILK/SILKCodec.cpp ${_silk})
target_include_directories(silk_ptplugin PRIVATE
  audio/SILK/SILK_SDK/interface
  audio/SILK/SILK_SDK/src)
target_link_libraries(silk_ptplugin PRIVATE ${OPAL_PLUGIN_LIBM})

file(GLOB _ilbc audio/iLBC/iLBC/*.c)
opal_add_plugin(iLBC audio audio/iLBC/ilbccodec.c ${_ilbc})
target_include_directories(iLBC_ptplugin PRIVATE audio/iLBC audio/iLBC/iLBC)

file(GLOB _isac audio/iSAC/iSAC/*.c)
opal_add_plugin(iSAC audio audio/iSAC/iSACcodec.c ${_isac})
target_include_directories(iSAC_ptplugin PRIVATE audio/iSAC/iSAC)
target_link_libraries(iSAC_ptplugin PRIVATE ${OPAL_PLUGIN_LIBM})

opal_add_plugin(gsmamrcodec audio
  audio/gsm-amr/amrcodec.c
  audio/gsm-amr/src/interf_enc.c
  audio/gsm-amr/src/interf_dec.c
  audio/gsm-amr/src/sp_dec.c
  audio/gsm-amr/src/sp_enc.c)
target_include_directories(gsmamrcodec_ptplugin PRIVATE audio/gsm-amr audio/gsm-amr/src)
target_compile_definitions(gsmamrcodec_ptplugin PRIVATE VAD1)
target_link_libraries(gsmamrcodec_ptplugin PRIVATE ${OPAL_PLUGIN_LIBM})

if(OPAL_PC_SPEEX_FOUND)
  opal_add_plugin(speex audio audio/Speex/speexcodec.cxx)
  target_compile_definitions(speex_ptplugin PRIVATE OPAL_SYSTEM_SPEEX=1)
  opal_link_pkg(speex_ptplugin OPAL_PC_SPEEX)
else()
  file(GLOB _speex audio/Speex/libspeex/*.c)
  # modes_noglobals.c is an alternate build of modes.c. jitter.c, stereo.c,
  # and the test programs are not part of the plugin.
  list(FILTER _speex EXCLUDE REGEX "/(modes_noglobals|jitter|stereo|test[^.]*).c$")
  opal_add_plugin(speex audio audio/Speex/speexcodec.cxx ${_speex})
  target_include_directories(speex_ptplugin PRIVATE audio/Speex/libspeex)
  target_compile_definitions(speex_ptplugin PRIVATE OPAL_SYSTEM_SPEEX=0)
  target_link_libraries(speex_ptplugin PRIVATE ${OPAL_PLUGIN_LIBM})
endif()

if(OPAL_PC_OPUS_FOUND)
  opal_add_plugin(opus audio audio/Opus/OpusCodec.cpp)
  opal_link_pkg(opus_ptplugin OPAL_PC_OPUS)
else()
  message(STATUS "OPAL audio plugin opus skipped; libopus was not found")
endif()

if(OPAL_PC_CELT_FOUND)
  opal_add_plugin(celt audio audio/celt/celtcodec.c)
  opal_link_pkg(celt_ptplugin OPAL_PC_CELT)
else()
  message(STATUS "OPAL audio plugin celt skipped; libcelt was not found")
endif()

# --- Fax ---

if(OPAL_PC_SPANDSP_FOUND)
  opal_add_plugin(spandsp fax fax/fax_spandsp/spandsp_fax.cpp)
  target_compile_definitions(spandsp_ptplugin PRIVATE __STDC_LIMIT_MACROS)
  opal_link_pkg(spandsp_ptplugin OPAL_PC_SPANDSP)
else()
  message(STATUS "OPAL fax plugin spandsp skipped; libspandsp was not found")
endif()

# --- Video codecs ---

set(_opal_ffmpeg_common
  video/common/encframe.cxx
  video/common/ffmpeg.cxx)

if(OPAL_PC_AVCODEC_FOUND)
  opal_add_plugin(h263_ffmpeg video
    video/H.263-1998/h263-1998.cxx
    video/H.263-1998/rfc2190.cxx
    video/H.263-1998/rfc2429.cxx
    ${_opal_ffmpeg_common})
  target_include_directories(h263_ffmpeg_ptplugin PRIVATE video/common)
  opal_link_pkg(h263_ffmpeg_ptplugin OPAL_PC_AVCODEC)

  opal_add_plugin(h264_ffmpeg video
    video/H.264-FFMPEG/h264-FFMPEG.cxx
    ${_opal_ffmpeg_common}
    video/common/h264frame.cxx)
  target_include_directories(h264_ffmpeg_ptplugin PRIVATE video/common)
  opal_link_pkg(h264_ffmpeg_ptplugin OPAL_PC_AVCODEC)

  opal_add_plugin(h265_ffmpeg video
    video/H.265-FFMPEG/h265-FFMPEG.cxx
    ${_opal_ffmpeg_common})
  target_include_directories(h265_ffmpeg_ptplugin PRIVATE video/common)
  opal_link_pkg(h265_ffmpeg_ptplugin OPAL_PC_AVCODEC)

  opal_add_plugin(mpeg4_ffmpeg video
    video/MPEG4-ffmpeg/mpeg4.cxx
    ${_opal_ffmpeg_common})
  target_include_directories(mpeg4_ffmpeg_ptplugin PRIVATE video/common)
  opal_link_pkg(mpeg4_ffmpeg_ptplugin OPAL_PC_AVCODEC)
else()
  message(STATUS "OPAL FFmpeg video plugins skipped; libavcodec was not found")
endif()

if(OPAL_PC_THEORA_FOUND)
  opal_add_plugin(theora video
    video/THEORA/theora_plugin.cxx
    video/THEORA/theora_frame.cxx)
  target_include_directories(theora_ptplugin PRIVATE video/common video/THEORA)
  opal_link_pkg(theora_ptplugin OPAL_PC_THEORA)
else()
  message(STATUS "OPAL video plugin theora skipped; libtheora was not found")
endif()

if(OPAL_PC_VPX_FOUND)
  opal_add_plugin(vp8_webm video video/VP8-WebM/vp8_webm.cxx)
  opal_link_pkg(vp8_webm_ptplugin OPAL_PC_VPX)
else()
  message(STATUS "OPAL video plugin vp8_webm skipped; libvpx was not found")
endif()

if(OPAL_PC_X264_FOUND AND OPAL_PC_AVCODEC_FOUND)
  opal_add_plugin(h264_x264 video
    video/x264/h264-x264.cxx
    video/x264/x264wrap.cxx
    video/common/h264frame.cxx
    video/common/encframe.cxx
    video/common/ffmpeg.cxx)
  target_include_directories(h264_x264_ptplugin PRIVATE video/common video/x264)
  target_compile_definitions(h264_x264_ptplugin PRIVATE
    X264_LICENSED
    "VID_PLUGIN_DIR=\"${CMAKE_BINARY_DIR}/plugins/video\"")
  if(NOT WIN32)
    target_compile_definitions(h264_x264_ptplugin PRIVATE HAVE_UNISTD_H)
  endif()
  opal_link_pkg(h264_x264_ptplugin OPAL_PC_X264)
  opal_link_pkg(h264_x264_ptplugin OPAL_PC_AVCODEC)
else()
  message(STATUS "OPAL video plugin h264_x264 skipped; libx264 or libavcodec was not found")
endif()

if(OPAL_PC_OPENH264_FOUND)
  set(_openh264_inc "")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/video/openh264/inc_2.3.1")
    set(_openh264_inc video/openh264/inc_2.3.1)
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/video/openh264/inc_2.2.0")
    set(_openh264_inc video/openh264/inc_2.2.0)
  endif()
  opal_add_plugin(openh264 video
    video/openh264/openh264.cxx
    video/common/encframe.cxx
    video/common/h264frame.cxx)
  target_include_directories(openh264_ptplugin PRIVATE video/common ${_openh264_inc})
  opal_link_pkg(openh264_ptplugin OPAL_PC_OPENH264)
else()
  message(STATUS "OPAL video plugin openh264 skipped; libopenh264 was not found")
endif()
