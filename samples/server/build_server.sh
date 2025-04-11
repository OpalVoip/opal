#!/bin/bash

set -e

MINOR_VERSION=20
INSTALLDIR=/usr/local

USAGE=true
BOOTSTRAP=false
UPDATE=false
RESTART=false
NO_INSTALL=false
CLEAN=false
BRANCH=""
MINIMAL=false
CONFIG_PARMS=""
MAKE_TARGET="optdepend opt"

while [ -n "$1" ]; do
    case "$1" in
        "--debug" )
            MAKE_TARGET="all"
        ;;

        "--restart" )
            RESTART=true
        ;;

        "--install" )
            shift
            INSTALLDIR="$1"
            CONFIG_PARMS+="--prefix=$INSTALLDIR "
        ;;

        "--no-install" )
            NO_INSTALL=true
        ;;

        "--clean" )
            CLEAN=true
        ;;

        "--branch" )
            shift
            BRANCH="$1"
        ;;

        "--minimal" )
            MINIMAL=true
            CONFIG_PARMS+="--disable-silk \
                           --disable-gsmamr \
                           --disable-g.726 \
                           --disable-ima-adpcm \
                           --disable-g.722 \
                           --disable-g.722.2 \
                           --disable-lpc10 \
                           --disable-isac \
                           --disable-g.722.1 \
                           --disable-gsm \
                           --disable-iLBC \
                           --disable-speex \
                           --disable-sbc \
                           --disable-celt "
        ;;

        "bootstrap" )
            USAGE=false
            BOOTSTRAP=true
            CLEAN=false
            shift
            break
        ;;

        "update" )
            USAGE=false
            UPDATE=true
            shift
            break
        ;;

        "buildonly" )
            USAGE=false
            shift
            break
        ;;
    esac
    shift
done

CONFIG_PARMS+="$@"


function ask() {
    read -p "$1 (y/N)? "
    if [ "$REPLY" != "Y" -a "$REPLY" != "y" ]; then
        echo "Aborted"
        exit
    fi
}


function clone() {
    echo "========================================================================"
    git clone -b $2 $GIT_PATH/$1
    cd $1
    git status
    cd ..
}


function pull() {
    cd $1
    echo "========================================================================"
    if [ -n "$(git status --short --untracked-files=no)" ]; then
        ask "$1 changed, reset --hard"
        git reset --hard
    fi
    [ -n "$BRANCH" ] && git checkout "$BRANCH"
    git pull --rebase
    cd ..
}


if $USAGE; then
    cat <<-EOM
	usage: `basename $0` [ options] { bootstrap | update | buildonly } [ <config-options> ... ]
	          --debug        Include debug version
	          --restart      Restart OPAL server daemon after install
	          --install <d>  Set install directory
	          --no-install   Do not install - local build only
	          --clean        Clean before build, including configuration
	          --branch <b>   Git branch to check out, defaults to v2_$MINOR_VERSION/v3_$MINOR_VERSION
	          --minimal      For bootstrap, do minimal install of packages
	          bootstrap      Do full OS install of pre-requisites and fresh download
	          update         Do git update and rebuild
	          buildonly      Only do build, do not do git update
	
	if the environment variable SOURCEFORGE_USERNAME is set, it will be used as the
	SourceForge account, and ssh used instead of https. Note, certificates must be
	set up for your local account to access SourceForge for this to work.
	EOM
    exit 1
fi

cd `dirname $0`
if pwd | grep samples/server; then
    cd ../../..
fi

if $BOOTSTRAP; then
    if [ -d ptlib -o -d opal ]; then
        ask "PTLib/OPAL already present, delete"
        rm -rf ptlib opal
    fi

    if which apt > /dev/null 2> /dev/null; then
        if $MINIMAL; then
            sudo apt install \
                g++ git make autoconf libexpat-dev libssl-dev \
                libsrtp2-dev libopus-dev
        elif [ $(lsb_release --release --short) \< "22.04" ]; then
            sudo apt install --ignore-missing \
                g++ git make autoconf swig \
                libncurses-dev libpcap-dev libexpat-dev libssl1.0-dev \
                libsasl2-dev libldap-dev unixodbc-dev libsdl2-dev \
                liblua5.3-dev libv8-dev \
                libavcodec-dev libavformat-dev libswscale-dev \
                libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
                libgsm1-dev libspeex-dev libopus-dev \
                libx264-dev libvpx-dev libtheora-dev libspandsp-dev \
                capiutils dahdi festival-dev libsrtp2-dev
        else
            sudo apt install --ignore-missing \
                g++ git make autoconf swig \
                libncurses-dev libpcap-dev libexpat1-dev libssl-dev \
                libsasl2-dev libldap-dev unixodbc-dev libsdl2-dev \
                liblua5.3-dev libnode-dev \
                libavcodec-dev libavformat-dev libswscale-dev \
                libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
                libgsm1-dev libspeex-dev libopus-dev \
                libx264-dev libopenh264-dev libvpx-dev libtheora-dev \
                libspandsp-dev libsrtp2-dev dahdi festival-dev
        fi
    elif which yum > /dev/null 2> /dev/null; then
        if $MINIMAL; then
            sudo yum install \
                gcc-g++ git make autoconf expat-devel openssl-devel \
                libsrtp opus-devel
        else
            sudo yum-config-manager --enable epel
            sudo yum install \
                gcc-c++ git make autoconf \
                ncurses-devel libpcap-devel expat-devel openssl-devel \
                cyrus-sasl-devel openldap-devel unixODBC-devel SDL2-devel \
                lua-devel v8-devel \
                avcodec-devel libavformat-devel libswscale-devel \
                gstreamer1.0-devel gstreamer-plugins-base1.0-devel \
                gsm-devel speex-devel opus-devel \
                x264-devel libvpx-devel theora-devel libspandsp-devel \
                capiutils dahdi festival-devel libsrtp
        fi
    elif which brew > /dev/null 2> /dev/null; then
        brew install \
            autoconf libpcap expat openssl@3 cyrus-sasl openldap unixodbc \
            ffmpeg gstreamer libgsm speex opus x264 libvpx theora spandsp \
            lua v8 sdl2
    else
        echo "What OS is this? No apt or yum!"
        exit 1
    fi

    if [ -n "$SOURCEFORGE_USERNAME" ]; then
        GIT_PATH=ssh://$SOURCEFORGE_USERNAME@git.code.sf.net/p/opalvoip
    else
        GIT_PATH=git://git.code.sf.net/p/opalvoip
    fi

    clone ptlib ${BRANCH:-v2_$MINOR_VERSION}
    clone opal ${BRANCH:-v3_$MINOR_VERSION}
fi

if $UPDATE; then
    pull ptlib
    pull opal
fi

if $NO_INSTALL; then
    export PTLIBDIR=`pwd`/ptlib
    export OPALDIR=`pwd`/opal
    export PKG_CONFIG_PATH=$PTLIBDIR:$OPALDIR
else
    export PKG_CONFIG_PATH=$INSTALLDIR/lib/pkgconfig
fi

if $BOOTSTRAP || [ $# -gt 0 ] ; then
    echo "========================================================================"
    echo "Configuring PTLib with $CONFIG_PARMS"
    cd ptlib
    make "CONFIG_PARMS=$CONFIG_PARMS" config
    git checkout configure
    rm -f configure~
    cd ..
fi

if $CLEAN; then
    echo "========================================================================"
    make -C ptlib clean
    make -C opal clean
    make -C opal/samples/server clean
    rm -f ptlib/include/ptlib_config.h opal/include/opal_config.h
fi

echo "========================================================================"
make -C ptlib $MAKE_TARGET
echo "----------------------------------------"
$NO_INSTALL || sudo -E make -C ptlib install

if $BOOTSTRAP || [ $# -gt 0 ]; then
    echo "========================================================================"
    echo "Configuring OPAL with $CONFIG_PARMS"
    cd opal
    make "CONFIG_PARMS=$CONFIG_PARMS" config
    git checkout configure plugins/configure src/csharp/* src/java/*
    rm -f configure~ plugins/configure~
    cd ..
fi

echo "========================================================================"
make -C opal $MAKE_TARGET
echo "----------------------------------------"
$NO_INSTALL || sudo -E make -C opal install
echo "========================================================================"
make -C opal/samples/server $MAKE_TARGET
echo "----------------------------------------"
$NO_INSTALL || sudo -E make -C opal/samples/server install
echo "========================================================================"

if $BOOTSTRAP; then
    echo "Cleaning up"
    cd ptlib
    git reset --hard
    cd ../opal
    git reset --hard
    cd ..
    echo "========================================================================"
fi

if $RESTART; then
    /opt/opalsrv/bin/stop.sh
    /opt/opalsrv/bin/start.sh
    echo "========================================================================"
fi

echo "Done"
