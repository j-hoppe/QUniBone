#!/bin/bash
#
# Downloads big disk images which do not fit into github Repositiry

FILES_URL=http://files.retrocmp.com/qunibone/

echo "This script updates non-Github files from "
echo "		$FILES_URL"
echo "It forces all local files also present on that repository to latest version."
echo "This will both:"
echo " - update/rollback all sources and scripts to latest published state."
echo "Files not in the repository are not changed."
echo "Disk and tape images are also NOT updated, only the compressed .gz versions change."
echo "To rollback to an .gz image, just delete the uncompressed version."
read -p "Are you sure to proceed [y/*] ? "
if [[ ! $REPLY =~ ^[Yy]$ ]] ; then
	echo "OK, abort."
	exit 1
fi

# Need .htaccess in files.retrocmp.com/qunibone/
#    # Activates "Directory-Indexing":
#    Options +Indexes

# Recursive downlaod with wget
# -nH --cutdirs=1 : suppress  "files.retrocmp.com/qunibone/" from path
# -R 'index.html\*' . suppresses  helper index.html?C=S;O=A etc.
# -q --show-progress: progress per file
wget --recursive --no-parent -nH --cut-dirs=1 -R 'index.html*' -q --show-progress $FILES_URL

# Create simple file links ("4_deploy") for QBone/UniBone variants ("4_deploy_q")
chmod +x *.sh
./qunibone-platform.sh

# Assure all shell scripts are executable
find . -name '*.sh' -exec chmod +x '{}' \;

