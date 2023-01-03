#!/bin/bash
#
# Removes sources from older Git revisions, which prohibit compiling of current release.
# Called by update-code.sh

cd ~/10.01_base/2_src/arm
rm -fv devices.info drives.info \
    storagecontroller.cpp storagecontroller.hpp \
    storagedrive.cpp storagedrive.hpp

cd ~/90_common/src
rm -fv getopt2.c getopt2.h \
    inputline.c inputline.h \
    namevaluelist.c namevaluelist.h





