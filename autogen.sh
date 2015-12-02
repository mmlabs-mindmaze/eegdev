#!/bin/sh -e

# Get absolute path of current and package path
execpath=$(pwd)
cd $(dirname $0)
packagepath=$(pwd)
cd $execpath


glmodules="$(grep -v '#' $packagepath/rpl_modules)"
gloptions="--local-dir=gnulib-local --lgpl --libtool"


# Get gnulib-tool path from cache
GL_CMD_CACHE=".gnulib-tool-cmd.cache"
if [ -f "$GL_CMD_CACHE" ] ; then
	GLTOOL=`cat $GL_CMD_CACHE`
else
	GLTOOL=gnulib-tool
fi


# Process options (output filename)
while getopts "g:" option
do
  case $option in
    g) cd $(dirname $OPTARG)
       GLTOOL="$(pwd)/$(basename $OPTARG)"
       cd $execpath
       ;;
    *) exit 1 ;;
  esac
done
shift $(($OPTIND - 1))


# run gnulib-tool --update from the package folder
cd $packagepath
if ! $GLTOOL  $gloptions --add-import $glmodules> gnulib.log ; then
	cat gnulib.log
	rm gnulib.log
	exit 1
fi
rm gnulib.log
echo "$GLTOOL" > $GL_CMD_CACHE

# Generate the build scripts
autoreconf -fi

