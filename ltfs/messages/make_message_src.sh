#!/bin/sh

set -e

KERNEL_NAME=`uname -s`
if [ "$KERNEL_NAME" = "Darwin" ]; then
	ICU_FRAMEWORK=/Library/Frameworks/ICU.framework
	export PATH=${PATH}:${ICU_FRAMEWORK}/Versions/Current/usr/bin
	export DYLD_LIBRARY_PATH=${ICU_FRAMEWORK}/Versions/Current/usr/lib
	GENRB=${ICU_FRAMEWORK}/Versions/Current/usr/bin/genrb
	PKGDATA=${ICU_FRAMEWORK}/Versions/Current/usr/bin/pkgdata
else
	GENRB=genrb
	PKGDATA=pkgdata
fi

if [ "$#" -ne "1" ]; then
	echo "Usage: $0 object_file"
	exit 1
fi

BASENAME=`echo $1 | sed -e 's/_dat\.o$//'`

cd ${BASENAME}

make_obj() {
	# Create a fresh work directory
	if [ -d work ]; then
		rm -rf work
	fi
	mkdir work

	# Generate files
	${GENRB} -d work -q *.txt
	cd work
	ls *.res >packagelist.txt

	# Modern ICU pkgdata requires an options file (-O) for static/dll modes
	PKGDATA_INC=${PKGDATA_INC:-/mingw64/lib/icu/current/pkgdata.inc}
	if [ -f "$PKGDATA_INC" ]; then
		PKGDATA_OPTS="-O $PKGDATA_INC"
	else
		PKGDATA_OPTS=
	fi

	case $KERNEL_NAME in
		MINGW*_NT*|MSYS_NT*)
			#
			# HP_mingw_BUILD
			#
			# We use dynamic libraries for the package data, so use the
			# -m dll switch
			#
			${PKGDATA} -p ${BASENAME} -m dll -q ${PKGDATA_OPTS} packagelist.txt >/dev/null
			
			# Modern ICU pkgdata emits a lib-prefixed DLL plus a real
			# import library. Keep the DLL name pkgdata embedded in the
			# import lib (lib${BASENAME}.dll) so the loader finds it.
			#
			cp lib${BASENAME}.dll.a ../../lib${BASENAME}.a
			cp lib${BASENAME}.dll ../../lib${BASENAME}.dll
			cp ${BASENAME}_dat.o ../../${BASENAME}_dat.o
			;;
		*)
			${PKGDATA} -p ${BASENAME} -m static -q ${PKGDATA_OPTS} packagelist.txt >/dev/null
			mv ${BASENAME}_dat.o ../../
			;;
	esac

	# Clean up
	cd ..
	rm -rf work
}

# Check whether we need to do anything
if [ -f "../$1" ]; then
	for file in *; do
		if [ "$file" -nt "../$1" ]; then
			make_obj
			exit 0
		fi
	done
else
	make_obj
fi
