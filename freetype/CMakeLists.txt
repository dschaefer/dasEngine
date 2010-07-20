cmake_minimum_required(VERSION 2.8)

include_directories(include cmake builds/unix)

add_definitions(-DFT_CONFIG_CONFIG_H=<ftconfig.h> -DFT2_BUILD_LIBRARY)

set(SOURCES
	src/base/ftbase.c
	src/base/ftbbox.c
	src/base/ftbitmap.c
	src/base/ftfstype.c
	src/base/ftgasp.c
	src/base/ftglyph.c
	src/base/ftgxval.c
	src/base/ftinit.c
	src/base/ftlcdfil.c
	src/base/ftmm.c
	src/base/ftotval.c
	src/base/ftpatent.c
	src/base/ftpfr.c
	src/base/ftstroke.c
	src/base/ftsynth.c
	src/base/ftsystem.c
	src/base/fttype1.c
	src/base/ftwinfnt.c
	src/base/ftxf86.c

	src/autofit/autofit.c
	src/bdf/bdf.c
	src/cache/ftcache.c
	src/cid/type1cid.c
	src/cff/cff.c
	src/gzip/ftgzip.c
	src/lzw/ftlzw.c
	src/pcf/pcf.c
	src/pfr/pfr.c
	src/psaux/psaux.c
	src/pshinter/pshinter.c
	src/psnames/psmodule.c
	src/raster/raster.c
	src/sfnt/sfnt.c
	src/smooth/smooth.c
	src/truetype/truetype.c
	src/type1/type1.c
	src/type42/type42.c
	src/winfonts/winfnt.c
	)

if(UNIX)
set(SOURCES ${SOURCES} src/base/ftdebug.c)
endif(UNIX)

if(WIN32)
set(SOURCES ${SOURCES} builds/win32/ftdebug.c)
endif(WIN32)

add_library(ft2 ${SOURCES})
