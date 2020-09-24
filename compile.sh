#/!bin/bash
# option "-a": recompile all from scratch
# else rely on makefile rules
#
# to be called after qunibone-platform.sh

. qunibone-platform.env
. compile-bbb.env

# makefile_u or makefile_q
MAKEFILE=makefile$PLATFORM_SUFFIX

# Debugging: remote from Eclipse. Compile on BBB is release.
export MAKE_CONFIGURATION=RELEASE

cd 10.03_app_demo/2_src

if [ "$1" == "-a" ] ; then
  make -f $MAKEFILE clean
fi

make -f $MAKEFILE
cd ~

echo "To run binary, call"
echo "10.03_app_demo/4_deploy/demo"

