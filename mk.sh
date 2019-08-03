#!/bin/bash
#DEBUG=
DEBUG="-g"
XFLAGS="-DHAVE_VTE_ASYNC"

if [ "$1" == "sync" ]; then
	XFLAGS="-UHAVE_VTE_ASYNC"
	shift
fi

g++ -Wno-deprecated-declarations $DEBUG $XFLAGS -o $2 -DWEBVIEW_GTK=1 $(pkg-config --cflags gtk+-3.0 webkit2gtk-4.0 vte-2.91) $1 $(pkg-config --libs gtk+-3.0 webkit2gtk-4.0 vte-2.91)

