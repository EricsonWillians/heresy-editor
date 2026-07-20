#!/bin/bash
set -e

if [ ! -d ports ]; then
  echo "Run this script from the top level."
  exit 1
fi

echo "Creating source package for Heresy Editor..."

dest="heresy-X.XX-source"

mkdir $dest

#
#  Source code
#
cp -av CMakeLists.txt cmake_uninstall.cmake.in build.sh $dest/

cp -av src $dest/src
cp -av osx $dest/osx
cp -av misc $dest/misc

mkdir $dest/obj_linux
mkdir $dest/obj_win32

#
#  Data files
#
cp -av bindings.cfg $dest
cp -av defaults.cfg $dest
cp -av operations.cfg $dest

cp -av common $dest/common
cp -av games  $dest/games
cp -av ports  $dest/ports

#
#  Documentation
#
cp -av *.txt $dest

cp -av docs $dest/docs
cp -av changelogs $dest/changelogs

#
# all done
#
echo "------------------------------------"
echo "All done."
