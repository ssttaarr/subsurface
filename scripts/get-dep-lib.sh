#!/bin/bash
#

# set version of 3rd party libraries
CURRENT_LIBZIP="1.2.0"
CURRENT_LIBGIT2="v0.26.0"
CURRENT_HIDAPI="hidapi-0.7.0"
CURRENT_LIBCURL="curl-7_54_1"
CURRENT_LIBUSB="v1.0.21"
CURRENT_OPENSSL="OpenSSL_1_1_0h"
CURRENT_LIBSSH2="libssh2-1.8.0"
CURRENT_XSLT="v1.1.29"
CURRENT_SQLITE="3190200"
CURRENT_LIBXML2="v2.9.4"
CURRENT_LIBFTDI="1.3"
CURRENT_KIRIGAMI="8b803828ad9ea67559a64a93c927862d6b19c96e"
CURRENT_BREEZE_ICONS=""

# Checkout library from git
# Ensure specified version is checked out,
# while avoiding cloning/fetching if unnecessary.
#
# Arguments:
#	name - used as directory name
#	version - any tag/sha usable by git
#	url - repository url
#
git_checkout_library() {
	[ $# -ne 3 ] && return 1

	# for clarity
	local name=$1
	local version=$2
	local url=$3

	if [ ! -d "$name" ]; then
		git clone "$url" "$name"
	fi
	pushd "$name"

	local current_sha=$(git rev-parse HEAD)
	local target_sha=$(git rev-parse "$version")

	if [ ! "$current_sha" = "$target_sha" ] ; then
		git fetch origin
		if ! git checkout "$version" ; then
			echo "Can't find the right tag in $name - giving up"
			return 1
		fi
	fi
	popd
}

# Download and extract tarball dependencies
#
# Arguments:
#	name - used as output directory
#	base_url -
#	filename - tarball file name
#
curl_download_library() {
	[ $# -ne 3 ] && return 1

	local name=$1
	local base_url=$2
	local filename=$3

	if [ ! -f "$filename" ]; then
		${CURL} "${base_url}${filename}"
	fi

	if [ ! -d "$name" ] || [ "$name" -ot "$filename" ] ; then
		rm -rf "$name"
		mkdir "$name"
		tar -C "$name" --strip-components=1 -xf "$filename"
	fi
}


# deal with all the command line arguments
if [ $# -ne 2 ] && [ $# -ne 3 ] ; then
	echo "wrong number of parameters, format:"
	echo "get-dep-lib.sh <platform> <install dir>"
	echo "get-dep-lib.sh single <install dir> <lib>"
	echo "get-dep-lib.sh singleAndroid <install dir> <lib>"
	echo "where"
	echo "<platform> is one of scripts, ios or android"
	echo "(the name of the directory where build.sh resides)"
	echo "<install dir> is the directory to clone in"
	echo "<lib> is the name to be cloned"
	exit -1
fi

PLATFORM=$1
INSTDIR=$2
if [ ! -d "${INSTDIR}" ] ; then
	echo "creating dir"
	mkdir -p "${INSTDIR}"
fi

# FIX FOR ANDROID,
if [ "$PLATFORM" == "singleAndroid" ] ; then
	CURRENT_LIBZIP="1.1.3"
	CURRENT_OPENSSL="OpenSSL_1_0_2o"
# If changing the openSSL version here, make sure to change it in versions.sh also.
fi
# no curl and old libs (never version breaks)
# check whether to use curl or wget
if [ "$(which curl)" == "" ] ; then
	CURL="wget "
else
	CURL="curl -O "
fi
COMMON_PACKAGES=(libzip libgit2 googlemaps)
case ${PLATFORM} in
	scripts)
		PACKAGES=("${COMMON_PACKAGES[@]}" hidapi libcurl libusb openssl libssh2)
		;;
	ios)
		PACKAGES=("${COMMON_PACKAGES[@]}" libxslt)
		;;
	android)
		PACKAGES=("${COMMON_PACKAGES[@]}" libxslt sqlite libxml2 openssl libftdi1 libusb)
		;;
	single)
		PACKAGES=("$3")
		;;
	singleAndroid)
		PACKAGES=("$3")
		;;
	*)
		echo "Unknown platform ${PLATFORM}, choose between native, ios or android"
		;;
esac

# show what you are doing and stop when things break
set -x
set -e

# get ready to download needed sources
cd "${INSTDIR}"

for package in "${PACKAGES[@]}" ; do
	case "$package" in
		libcurl)
			git_checkout_library libcurl $CURRENT_LIBCURL https://github.com/curl/curl.git
			;;
		libgit2)
			git_checkout_library libgit2 $CURRENT_LIBGIT2 https://github.com/libgit2/libgit2.git
			;;
		libssh2)
			git_checkout_library libssh2 $CURRENT_LIBSSH2 https://github.com/libssh2/libssh2.git
			;;
		libusb)
			git_checkout_library libusb $CURRENT_LIBUSB https://github.com/libusb/libusb.git
			;;
		libxml2)
			git_checkout_library libxml2 $CURRENT_LIBXML2 https://github.com/GNOME/libxml2.git
			;;
		libxslt)
			git_checkout_library libxslt $CURRENT_XSLT https://github.com/GNOME/libxslt.git
			;;
		breeze-icons)
			git_checkout_library breeze-icons master https://github.com/kde/breeze-icons.git
			;;
		googlemaps)
			git_checkout_library googlemaps master https://github.com/Subsurface-divelog/googlemaps.git
			;;
		hidapi)
			git_checkout_library hidapi master https://github.com/signal11/hidapi.git
			;;
		kirigami)
			git_checkout_library kirigami $CURRENT_KIRIGAMI https://github.com/KDE/kirigami.git
			;;
		openssl)
			git_checkout_library openssl $CURRENT_OPENSSL https://github.com/openssl/openssl.git
			;;
		libzip)
			curl_download_library libzip https://subsurface-divelog.org/downloads/ libzip-${CURRENT_LIBZIP}.tar.xz
			;;
		libftdi1)
			curl_download_library libftdi1 https://www.intra2net.com/en/developer/libftdi/download/ libftdi1-${CURRENT_LIBFTDI}.tar.bz2
			;;
		sqlite)
			curl_download_library sqlite https://www.sqlite.org/2017/ sqlite-autoconf-${CURRENT_SQLITE}.tar.gz
			;;
		*)
			echo "unknown package \"$package\""
			exit 1
			;;
	esac
done
