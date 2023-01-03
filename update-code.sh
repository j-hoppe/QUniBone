#!/bin/bash
#
# arg 1: Branch. "master", if none
GITREPO=j-hoppe/QUniBone
GITBRANCH=master
if [ ! -z "$1" ] ; then
    GITBRANCH=$1
fi

echo "This script updates program sources from GitHub"
echo "		https://github.com/$GITREPO/tree/$GITBRANCH"
echo "It forces all local files also present on GitHub to latest version,"
echo "then a full recompile is started."
echo "This will update/rollback all sources and scripts to latest published state."
echo "Also script ./cleanup.sh runs and deletes some conflicting stuff... check it out."
echo "Other files not (anymore) on GitHub are not touched."
read -p "Are you sure [y/*] ? "
if [[ ! $REPLY =~ ^[Yy]$ ]] ; then
	echo "OK, abort."
	exit 1
fi

ARCHIVE=$GITBRANCH.tar.gz
rm -f $ARCHIVE
wget -nv --show-progress https://github.com/$GITREPO/archive/$ARCHIVE
if [ $? -ne 0 ] ; then
    echo "Error downloading sources from https://github.com/$GITREPO/archive/$ARCHIVE !"
    exit 1
fi

# now we have a single file $GITBRANCH here. remove the special tar-tree rootdir
# This will not clear outdated files, they will remain as junk.
tar xzvf $ARCHIVE --strip-components=1


# make downloaded scripts executable
chmod +x *.sh

echo "Deleting now outdated and conflicting files ..."
./cleanup.sh

# Create simple file links ("4_deploy") for QBone/UniBone variants ("4_deploy_q")
./qunibone-platform.sh

# Assure all shell scripts are executable
find . -name '*.sh' -exec chmod +x '{}' \;

# Start recompile.
./compile.sh -a
