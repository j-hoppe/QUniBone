#! /bin/bash
#
# Personalizes GitHub "QUniBone" software tree to "UniBOne" or "QBone" hardware.
# Needs hardware-specific settings in "qunibone-platform.env".
#
# Generates simple links to
# Example:
# on UniBone, "compile.sh" is a link to "compile_u.sh"
# on QBone, it is a link to "compile_q.sh"


#set -v
#set -x
#set -u
# requests a <enter> afte each line
#trap read debug

# Are we running on am UniBone or a QBone hardware ?
PLATFORMENV="qunibone-platform.env"
if [ ! -f $PLATFORMENV ]; then
  echo "Error: Platform settings in file $PLATFORMENV not found!"
  exit 1
fi


. $PLATFORMENV

if [ -z "$PLATFORM_SUFFIX" ]; then
  echo "Error: variable PLATFORM_SUFFIX not set or empty!"
  exit 1
fi


#################################################################
# make_link()
# create symbolic link "linkpath" for "filepath"
# linkpath & filepath result of "link4*" functions
function make_link {

  if [ "$filepath" == "$linkpath" ]; then
    echo "Error: no _u/_q variant for $linkpath found!"
    exit 1
  fi

  # is there a file or directory containing  PLATFORM_SUFFIX?
  if [ ! -f "$filepath" ] && [ ! -d "$filepath" ] ; then
    echo "Error: $filepath does not exist!"
    exit 1
  fi

  # any match: create the link
  ln -f -s $filepath $linkpath
  echo "Created link $linkpath for $filepath"
}

#################################################################
# link4sh()
# create link for _u/_q shell file
function link4sh() {
  # parameter: name of simpler name to _u.sh or _q.sh file
  linkpath="$1"

  # see https://tldp.org/LDP/abs/html/string-manipulation.html
  # try to match "dir/filename_u.sh"

  substr=.sh
  replace=${PLATFORM_SUFFIX}.sh
  # find at end of file
  filepath=${linkpath/%$substr/$replace}

  make_link
  return $?
}

#################################################################
# link4dir()
# create link for _u/_q directory, which is created if missing
# Example:
# ln -s  10.03_app_demo/4_deploy_q 10.03_app_demo/4_deploy
function link4dir() {
  # try suffix at end of file/directoy: 4_deploy_u -> 4_deploy
  linkpath="$1"

  substr=
  replace=${PLATFORM_SUFFIX}
  filepath=${linkpath/%$substr/$replace}

  mkdir -p $filepath

  make_link
  return $?
}

#################################################################
# main()

# link4sh ./compile.sh

link4dir $HOME/10.03_app_demo/4_deploy


