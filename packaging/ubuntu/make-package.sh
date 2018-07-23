#!/bin/bash -e
# start from the directory above the combined subsurface & subsurface/libdivecomputer directory
#
# in order to be able to make changes to the debian/* files without changing the source
# this script assumes that the debian/* files plus a separate debian.changelog are in
# this directory as well - this makes testing builds on launchpad easier
# for most people all it should take is to run
#   cp -a packaging/ubuntu/debian .
#
if [[ $(pwd | grep "subsurface$") || ! -d subsurface || ! -d subsurface/libdivecomputer || ! -d subsurface/libgit2 ]] ; then
	echo "Please start this script from the folder ABOVE the subsurface source directory"
	echo "which includes libdivecomputer and libgit2 as subdirectories)."
	exit 1;
fi

GITVERSION=$(cd subsurface ; git describe --abbrev=12 | sed -e 's/-g.*$// ; s/^v//')
GITREVISION=$(echo $GITVERSION | sed -e 's/.*-// ; s/.*\..*//')
VERSION=$(echo $GITVERSION | sed -e 's/-/./')
LIBDCREVISION=$(cd subsurface/libdivecomputer ; git rev-parse --verify HEAD)

#
#
echo "building Subsurface" $VERSION "with libdivecomputer" $LIBDCREVISION
#
if [[ ! -d subsurface_$VERSION ]]; then
	mkdir subsurface_$VERSION
	if [[ "x$GITREVISION" != "x" ]] ; then
		rm -f subsurfacedaily-$VERSION
		ln -s subsurface_$VERSION subsurfacedaily-$VERSION
	else
		rm -f subsurfacebeta-$VERSION
		ln -s subsurface_$VERSION subsurfacebeta-$VERSION
	fi

	#
	#
	echo "copying sources"
	#
	(cd subsurface ; tar cf - . ) | (cd subsurface_$VERSION ; tar xf - )
	cd subsurface_$VERSION;
	rm -rf .git libdivecomputer/.git libgit2/.git
	echo $GITVERSION > .gitversion
	echo $LIBDCREVISION > libdivecomputer/revision
	# dh_make --email dirk@hohndel.org -c gpl2 --createorig --single --yes -p subsurface_$VERSION
	# rm debian/*.ex debian/*.EX debian/README.*
	#
	#
	echo "creating source tar file for OBS and Ununtu PPA"
	#
	if [[ "x$GITREVISION" != "x" ]] ; then
		(cd .. ; tar ch subsurfacedaily-$VERSION | xz > home:Subsurface-Divelog/Subsurface-daily/subsurface-$VERSION.orig.tar.xz) &
	else
		(cd .. ; tar ch subsurfacebeta-$VERSION | xz > home:Subsurface-Divelog/Subsurface-beta/subsurface-$VERSION.orig.tar.xz) &
	fi
	tar cf - . | xz > ../subsurface_$VERSION.orig.tar.xz
else
	echo "using existing source tree"
	cd subsurface_$VERSION
fi
#
#
echo "preparint the debian directory"
#
export DEBEMAIL=dirk@hohndel.org
rm -rf debian
mkdir -p debian
cp -a ../debian .
cp ../debian.changelog debian/changelog

rev=0
while [ $rev -le "99" ]
do
rev=$(($rev+1))
	if [[ ! $(grep $VERSION-$rev debian/changelog) ]] ; then
		break
	fi
done
if [[ "$RELEASE" = "1" ]] ; then
	dch -v $VERSION-$rev~xenial -D xenial -M -m "Next release build - please check https://subsurface-divelog.org/category/news/ for details"
else
	dch -v $VERSION-$rev~xenial -D xenial -M -m "next daily build"
fi
mv ~/src/debian.changelog ~/src/debian.changelog.previous
cp debian/changelog ~/src/debian.changelog

debuild -S

#create builds for the newer Ubuntu releases that Launchpad supports
rel=xenial
others="artful bionic"
for next in $others
do
	sed -i "s/${rel}/${next}/g" debian/changelog
	debuild -S
	rel=$next
done

cd ..

if [[ "$1x" = "postx" ]] ; then
	# daily vs. beta vs. release
	if [[ "x$GITREVISION" == "x" ]] ; then
		# this is a beta or a release; assume beta for now and deal with release later :-)
		dput ppa:subsurface/subsurface-beta subsurface_$VERSION-$rev~*.changes
		cd home:Subsurface-Divelog/Subsurface-beta
		osc rm $(ls subsurface*.tar.xz | grep -v $VERSION)
		osc add subsurface-$VERSION.orig.tar.xz
		sed -i "s/%define latestVersion.*/%define latestVersion $VERSION/" subsurfacebeta.spec
		sed -i "s/%define gitVersion .*/%define gitVersion 0/" subsurfacebeta.spec
		osc commit -m "next beta build"
	else
		dput ppa:subsurface/subsurface-daily subsurface_$VERSION-$rev~*.changes
		cd home:Subsurface-Divelog/Subsurface-daily
		osc rm $(ls subsurface*.tar.xz | grep -v $VERSION)
		osc add subsurface-$VERSION.orig.tar.xz
		sed -i "s/%define latestVersion.*/%define latestVersion $VERSION/" subsurfacedaily.spec
		sed -i "s/%define gitVersion .*/%define gitVersion $GITREVISION/" subsurfacedaily.spec
		osc commit -m "next daily build"
	fi
fi
