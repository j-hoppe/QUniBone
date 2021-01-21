#!/bin/bash
#

if [ -f ./update-code.sh ] ; then
    echo "$0 is deprecated, execute \"./update-code.sh\" instead."
    echo "Update disk images and emulation scripts with \"./update-files.sh\"."
    exit 1
fi

GITURL=https://github.com/j-hoppe/QUniBone.git
echo "This script updates local files from GitHub"
echo "		$GITURL"
echo "It forces all local files also present on GitHub to latest version,"
echo "then a full recompile is started."
echo "This will both:"
echo " - update/rollback all sources and scripts to latest published state."
echo "Files not (anymore) on GitHub are not touched."
read -p "Are you sure [y/*] ? "
if [[ ! $REPLY =~ ^[Yy]$ ]] ; then
	echo "OK, abort."
	exit 1
fi

# make sure we have svn
sudo apt install subversion

# download from github without creating repository
svn export --force ${GITURL}/trunk  .
# This will not clear outdated files, they will remain as junk.

# Create simple file links ("4_deploy") for QBone/UniBone variants ("4_deploy_q")
chmod +x *.sh
./qunibone-platform.sh

# Assure all shell scripts are executable
find . -name '*.sh' -exec chmod +x '{}' \;

# Start recompile.
./compile.sh -a

echo "Compile complete."
echo "Update disk images and emulation scripts with \"./update-files.sh\"."
