#!/bin/bash

# one can't do 'docker run <image> <cmd1> && <cmd2>', so we need this teensy wrapper.
set -ex
if [ -z $BUILD_DIR ] ; then
    echo "You must set BUILD_DIR so we are building from the right place"
    exit 1
fi
cd $BUILD_DIR
perl build.pl
dpkg-buildpackage -d -b
