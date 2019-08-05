CXX?=g++
DEBUG?=-g
CXXFLAGS+=$(DEBUG) -Wno-deprecated-declarations  $(shell pkg-config --cflags gtk+-3.0 webkit2gtk-4.0 vte-2.91)
LDLIBS+=$(shell pkg-config --libs gtk+-3.0 webkit2gtk-4.0 vte-2.91)
PROGNAME?=goobyterm

$(PROGNAME): main.cc config.h icon.h about.h
ifeq ($(SYNC),)
	XFLAGS="-UHAVE_VTE_ASYNC"
else
	XFLAGS="-DHAVE_VTE_ASYNC"
endif
	$(CXX) $(CXXFLAGS) $(XFLAGS) -o $@ $< $(LDLIBS)

icon.h: icon240x240.png
	xxd -i $< > $@

about.h: about.html
	xxd -i $< > $@

