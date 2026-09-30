Building OPAL with CMake
========================

CMake builds OPAL on macOS, Windows, Linux, MinGW-w64, Android, and iOS.
The library target is opal, and consumers use the imported target OPAL::opal.
On macOS and Linux the shared library is libopal, a debug build is libopal_d,
and a static build adds _s (libopal_s or libopal_d_s). 64-bit MSVC installs
opal64 (opal64d for Debug); a static build is opal64s and opal64sd. Other
Windows toolchains keep opal.dll. OPAL links
PTLib::ptlib, so build PTLib with CMake first. See README.Cmake in the
PTLib tree.

Requirements:
  CMake 3.20 or newer
  a C++20 compiler
  a C11 compiler
  a PTLib CMake build or install

The default build is a shared Release library with codec plugins. Sample
programs are off until -DOPAL_BUILD_SAMPLES=ON. Protocol features follow
the options in cmake/OPALOptions.cmake. A feature that needs something
PTLib was built without is turned off. Set that option to ON to make the
missing PTLib feature a configure error.

SRTP uses libsrtp2 when PTLib has OpenSSL. Acoustic echo cancellation
uses speexdsp when it is present, and the bundled Speex code otherwise.
CAPI is left out when capi20.h is absent. Codec plugins are described
below.

"cmake -LH build" lists the OPAL_* options.


Common commands
---------------

From the top of the OPAL tree, with <ptlib> either a PTLib install prefix
or a PTLib CMake build directory that contains PTLibConfig.cmake:

  cmake -S . -B build -DOPAL_PTLIB_DIR=<ptlib>
  cmake --build build
  cmake --install build --prefix /usr/local

Remove the files from that install with:

  cmake --build build --target uninstall

Use the same privileges as the install. A prefix such as /usr/local needs
sudo on both commands. uninstall reads install_manifest.txt in the build
tree, so it removes the last prefix this tree was installed to. Empty
directories created for those files are removed. The prefix itself is left
in place.

Useful options, passed on the first cmake command:

  -DBUILD_SHARED_LIBS=OFF       static library
  -DOPAL_BUILD_SAMPLES=ON       sample programs, including OpenPhone
                                when wx-config and wxrc are found
  -DOPAL_PLUGINS=OFF            library only, no codec plugins
  -DOPAL_T38_CAPABILITY=OFF     omit T.38 and fax, including the
                                SpanDSP plugin and the fax sample
  -DOPAL_OPALMIN=ON             omit obsolete codecs, T.38, fax,
                                RFC 4175, T.120, Skinny, and line
                                interface devices (LID, CAPI, DAHDI)
  -DOPAL_OBSOLETE_CODECS=OFF    omit Speex, iLBC, LPC-10, H.261,
                                G.722, G.722.1, G.722.2, G.726, G.728,
                                GSM 06.10, Theora, G.723.1, and
                                VoiceAge G.729
  -DCMAKE_BUILD_TYPE=Debug      single-config generators (Ninja, Makefiles)

Single-config generators also accept:

  cmake --build build --target debug
  cmake --build build --target both

"debug" configures a second tree in build/debug with CMAKE_BUILD_TYPE=Debug.
That build keeps PTRACE, the same way PTLib's debug target does. When
<ptlib>/debug already contains a PTLib CMake package, the debug tree links
that library. "both" builds the default tree and the debug tree.

Visual Studio and other multi-config generators use --config instead:

  cmake --build build --config Release
  cmake --build build --config Debug

Install produces:
  the public headers and opal_config.h
  the shared or static library
  lib/pkgconfig/opal.pc, which requires ptlib
  lib/cmake/OPAL/OPALConfig.cmake
  codec plugins under lib/opal-<version>/{audio,video,fax}

MinGW and Cygwin install plugins into a plugins directory under the
prefix. A program looks there when it is started with that layout.

The build directory is also a CMake package, so a downstream project can
use it without an install step:

  find_package(OPAL CONFIG REQUIRED)
  target_link_libraries(myapp PRIVATE OPAL::opal)

Point OPAL_DIR at that build directory, or pass it as a prefix. OPALConfig.cmake
loads PTLib, so the PTLib package has to be findable too. Set PTLib_DIR when
PTLib was not installed.

Plugins in the build tree land in build/plugins/{audio,video,fax}. The
path compiled into libopal is the install directory. To load the plugins
from a build tree, point PTLIBPLUGINDIR at that plugins directory:

  export PTLIBPLUGINDIR="$PWD/build/plugins"

OpenPhone is part of the sample build when wxWidgets supplies both
wx-config and wxrc. Homebrew's wxWidgets package provides both.


Codec plugins
-------------

Plugins are on by default. -DOPAL_PLUGINS=OFF builds libopal alone.
"cmake --build build" builds every plugin that configure accepted.
Each plugin is a loadable module named <codec>_ptplugin. The suffix is
.dylib on macOS, .dll on Windows and Cygwin, and .so elsewhere. Plugins
link PTLib. They are loaded by libopal at run time and are not part of
the libopal link line.

Build one plugin after configure:

  cmake --build build --target opus_ptplugin

On Visual Studio add --config Release. The CMake target is the file name
without the suffix, for example h264_ffmpeg_ptplugin.

Build-tree layout:

  build/plugins/audio/<codec>_ptplugin.<suffix>
  build/plugins/video/<codec>_ptplugin.<suffix>
  build/plugins/fax/<codec>_ptplugin.<suffix>

Install copies that tree to lib/opal-<version>/{audio,video,fax}. MinGW
and Cygwin use plugins/{audio,video,fax} under the install prefix. The
directory compiled into libopal is the install path. A program run from
the build tree loads plugins when PTLIBPLUGINDIR points at build/plugins:

  export PTLIBPLUGINDIR="$PWD/build/plugins"

On Windows set PTLIBPLUGINDIR to the same plugins directory. OPAL walks
those audio, video, and fax subdirectories and loads files whose name
ends in _ptplugin and whose suffix matches the platform.

These plugins are built from sources in the tree:

  audio: g722, g7221, g7222, g726, gsm0610, gsmamrcodec, iLBC, iSAC,
         ima_adpcm, lpc10, silk
  video: h261_vic

Speex is built from the system library when pkg-config finds speex, and
from plugins/audio/Speex/libspeex otherwise. The target is speex_ptplugin
in either case. -DOPAL_OBSOLETE_CODECS=OFF omits Speex, iLBC, LPC-10, H.261,
G.722, G.722.1, G.722.2, G.726, G.728, GSM 06.10, Theora, G.723.1, and VoiceAge G.729.
SpeexDSP acoustic echo cancellation stays. On Windows, G.723.1 ACM is built
with that switch left on, and VoiceAge G.729 is built when va_g729/va_g729a.h
has been unpacked beside the plugin.

These plugins are added when pkg-config finds the named module. Configure
prints a skip line when the module is absent. Install the library and run
cmake again to pick it up.

  opus          pkg-config opus
  celt          pkg-config celt
  spandsp       pkg-config spandsp          (fax/spandsp_ptplugin)
  h263_ffmpeg   pkg-config libavcodec
  h264_ffmpeg   pkg-config libavcodec
  h265_ffmpeg   pkg-config libavcodec
  mpeg4_ffmpeg  pkg-config libavcodec
  theora        pkg-config theoraenc theoradec ogg
  vp8_webm      pkg-config vpx
  h264_x264     pkg-config x264 and libavcodec
  openh264      pkg-config openh264

The H.264 FFmpeg plugin uses whatever H.264 encoder libavcodec provides,
including libx264 built into FFmpeg. h264_x264 links libx264 directly.
openh264 uses the headers under plugins/video/openh264/inc_* and the
installed libopenh264.

SBC is built only when plugins/audio/SBC contains the BlueZ SBC sources.
The copies in the repository are placeholders, so configure skips it
until those sources are present.


vcpkg
-----

vcpkg.json lists the optional libraries. PTLib has its own manifest in
the PTLib tree; use the same toolchain file for both configures. The
default features are libsrtp and speexdsp. Point CMake at a vcpkg checkout:

  cmake -S . -B build -DOPAL_PTLIB_DIR=<ptlib> -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake

Extra features, comma separated:

  -DVCPKG_MANIFEST_FEATURES=opus,speex,ffmpeg,vpx,theora,x264,openh264

-DOPAL_BUILD_SAMPLES=ON also enables the wxwidgets feature, which supplies
wxWidgets and wxrc for OpenPhone. A separate "vcpkg install wxwidgets" is
not visible in manifest mode. Reconfigure after changing features so vcpkg
can install them. wxrc is taken from that install's tools/wxwidgets directory.

CELT and SpanDSP are not in the vcpkg registry, so those two plugins
still need a library CMake can find with pkg-config.


macOS
-----

Install Xcode or the Xcode command line tools, then CMake. Homebrew is the
usual source of Opus, Speex, FFmpeg, Theora, libvpx, x264, libsrtp, and
wxWidgets. Build PTLib, then from the OPAL tree:

  cmake -S . -B build -DOPAL_PTLIB_DIR=<ptlib> -DOPAL_BUILD_SAMPLES=ON
  cmake --build build
  cmake --install build --prefix "$HOME/opal"

Pass -DCMAKE_PREFIX_PATH if the codec libraries are installed outside the
default search path, for example /opt/homebrew on Apple silicon. Use the
same prefix PTLib was configured with so both builds see the same
libraries.


Windows
-------

Use Visual Studio 2022 or later, with the C++ desktop workload, and CMake.
Build PTLib with the same generator, then generate an x64 solution:

  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DOPAL_PTLIB_DIR=<ptlib>
  cmake --build build --config Release
  cmake --install build --config Release --prefix C:\opal

"cmake --help" lists the generator name for a newer Visual Studio.
Multi-config builds pass --config Release or --config Debug on both the
build and the install command.

Optional libraries are easiest through vcpkg. Add the toolchain file to
the first command. See the vcpkg section for the codec features.

  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake


Linux
-----

Install CMake, a C++20 compiler, pkg-config, and the development packages
for the codecs you want. On Debian and Ubuntu that is typically
build-essential, cmake, pkg-config, libopus-dev, libspeex-dev,
libspeexdsp-dev, libavcodec-dev, libtheora-dev, libvpx-dev, libx264-dev,
and libsrtp2-dev. wxWidgets is needed for OpenPhone.

  cmake -S . -B build -DOPAL_PTLIB_DIR=<ptlib>
  cmake --build build
  sudo cmake --install build --prefix /usr/local


MinGW-w64
---------

Use the 64-bit MinGW-w64 toolchain, and a PTLib build made with that same
toolchain. Plugins are installed into a plugins directory next to the
process.

From an MSYS2 mingw64 shell, where gcc is already x86_64-w64-mingw32-gcc:

  cmake -S . -B build -G Ninja -DOPAL_PTLIB_DIR=<ptlib>
  cmake --build build
  cmake --install build --prefix /mingw64

Cross compiling from Linux:

  cmake -S . -B build-mingw \
    -DOPAL_PTLIB_DIR=<ptlib-mingw> \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_FIND_ROOT_PATH=/usr/x86_64-w64-mingw32
  cmake --build build-mingw

CMAKE_FIND_ROOT_PATH is the prefix that holds the MinGW builds of the
codec libraries. CMake will not use the host Linux copies. <ptlib-mingw>
is a PTLib build configured for the same cross compiler.

Cross compiling from macOS, after "brew install mingw-w64". Delete
build-mingw first if a previous configure already cached the host ar.
CMake otherwise picks /usr/bin/ar, which cannot read the response files
it writes for a Windows link, and the compiler test fails.

  cmake -S . -B build-mingw \
    -DOPAL_PTLIB_DIR=<ptlib-mingw> \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_C_COMPILER=/opt/homebrew/bin/x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/x86_64-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=/opt/homebrew/bin/x86_64-w64-mingw32-windres \
    -DCMAKE_AR=/opt/homebrew/bin/x86_64-w64-mingw32-ar \
    -DCMAKE_RANLIB=/opt/homebrew/bin/x86_64-w64-mingw32-ranlib
  cmake --build build-mingw

On an Intel Mac the Homebrew prefix is /usr/local instead of /opt/homebrew.
CMAKE_AR and CMAKE_RANLIB must be set on this first configure.


Android
-------

Use the Android NDK toolchain file, and a PTLib static library built with
the same ABI and platform. Desktop packages on the host are not used.

  cmake -S . -B build-android \
    -DOPAL_PTLIB_DIR=<ptlib-android> \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DBUILD_SHARED_LIBS=OFF
  cmake --build build-android

ANDROID_ABI is one of arm64-v8a, armeabi-v7a, x86_64, or x86. Repeat the
configure and build once per ABI. Codec plugins that need a host
pkg-config library are skipped. Put Android builds of those libraries on
CMAKE_PREFIX_PATH and CMAKE_FIND_ROOT_PATH when you want them.


iOS
---

Configure with CMake's iOS system name. This requires Xcode and a PTLib
build made with the same SDK and architectures.

  cmake -S . -B build-ios -G Xcode \
    -DOPAL_PTLIB_DIR=<ptlib-ios> \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_OSX_ARCHITECTURES=arm64
  cmake --build build-ios --config Release

The device SDK is the default. The simulator uses:

  -DCMAKE_OSX_SYSROOT=iphonesimulator

Host Homebrew libraries are not used for an iOS build.
