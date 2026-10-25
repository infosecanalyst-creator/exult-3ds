include Makefile.common
SRC:=.
CXX:=g++
CXXFLAGS:=-std=c++17 -O1 -w
CPPFLAGS:=-DEXULT_DATADIR=\"data\" -DHAVE_SYS_TYPES_H=1 -DVERSION=\"$(VERSION)\" -I. -Iheaders -Ifiles -Ifiles/zip -Iconf -Iimagewin -Ishapes -Iusecode -Itools -Idata
all: data/exult_flx.h
