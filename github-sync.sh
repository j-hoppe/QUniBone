#!/bin/bash
#
GITURL=https://github.com/j-hoppe/Q     UniBone.git
echo "This script updates local files from GitHub"
echo "		$GITURL"
echo "It forces all local files also present on GitHub to latest version,"
echo "then a full recompile is started."
echo "This will both:"
echo " - update all sources and scripts to latest published state."
echo " - roll back local changes made in scripts and some disk images."
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

# Generating shortcuts for demo scripts in ~ home directory
find 10.03_app_demo/5* -name \*.sh -exec ln -sf {} $HOME \;

# Assure all shell scripts are executable
find . -name '*.sh' -exec chmod +x '{}' \;

# Start recompile.
./compile.sh -a
