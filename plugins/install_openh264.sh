#!/bin/bash

set -e

VERSION=2.3.1

FORCE=false
if [ "$1" == "--force" ]; then
    FORCE=true
    shift
elif [ $(whoami) != root ]; then
    echo "$(basename $0) must be run as root!"
    exit 1
fi

if [ "$1" == "--debug" ]; then
    set -x
    shift
fi

if [ "$1" == "--version" ]; then
    VERSION="$2"
    shift 2
fi

if [ $(echo -e "${VERSION}\n2.3.1" | sort --version-sort | tail -1) == "$VERSION" ]; then
    LIBCVER=7
else
    LIBCVER=6
fi

PLATFORM=$(uname -sm)

case "$PLATFORM" in
    Linux* )
        LIBEXT=so
        LIBDIR=/usr/lib64
        FILE_LIBCVER=.$LIBCVER
        LINK_LIBCVER=${LIBEXT}.${LIBCVER}
    ;;
    Darwin* )
        LIBEXT=dylib
        LIBDIR=/usr/local/lib
        LINK_LIBCVER=${LIBCVER}.${LIBEXT}
    ;;
    * )
        echo "Unsupported platform $PLATFORM"
        exit 2
    ;;
esac

case "$PLATFORM" in
    Linux*x86_64 )
        PLATFORM=linux64
    ;;
    Linux*x86 )
        PLATFORM=linux32
        LIBDIR=/usr/lib
    ;;
    Linux*aarch64 )
        PLATFORM=linux-arm64
    ;;
    Darwin*x86_64 )
        PLATFORM=mac-x64
    ;;
    Darwin*arm64 )
        PLATFORM=mac-arm64
    ;;
    * )
      echo "Unsupported architecture $PLATFORM"
      exit 2
    ;;
esac

LIBBASE=libopenh264
LIBFILE=${LIBBASE}-${VERSION}-${PLATFORM}${FILE_LIBCVER}.${LIBEXT}

if [ -n "$1" ]; then
    LIBDIR="$1"
fi

if [ \! -d $LIBDIR ]; then
    mkdir -p $LIBDIR
fi

cd $LIBDIR
if [ -e "$LIBFILE" ] && ! $FORCE; then
    echo Already installed.
else
    echo Installing $LIBBASE to $LIBDIR.
    rm -f ${LIBBASE}*
    curl --silent http://ciscobinary.openh264.org/$LIBFILE.bz2 | bunzip2 > $LIBFILE
    chmod +x $LIBFILE
    ln -s $LIBFILE ${LIBBASE}.${LINK_LIBCVER}
    ln -s ${LIBBASE}.${LINK_LIBCVER} ${LIBBASE}.${LIBEXT}
fi
ls -l $LIBDIR/${LIBBASE}*
