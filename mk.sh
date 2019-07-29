#!/bin/bash
BASEDIR="$(dirname $(realpath $0))"
echo "BASEDIR=$BASEDIR"

#DEBUG=
DEBUG="-g"
XFLAGS="-DHAVE_VTE_ASYNC"

if [ "$1" == "old" ]; then
	XFLAGS="-UHAVE_VTE_ASYNC"
	shift
fi

g++ -Wno-deprecated-declarations $DEBUG $XFLAGS -I "$BASEDIR" -I /usr/include/vte-2.91 -o $2 -DWEBVIEW_GTK=1 $(pkg-config --cflags gtk+-3.0 webkit2gtk-4.0 vte-2.91) $1 $(pkg-config --libs gtk+-3.0 webkit2gtk-4.0 vte-2.91)

