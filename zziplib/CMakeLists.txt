cmake_minimum_required(VERSION 2.8)

include_directories(. ../zlib ../zlib/dasbuild/msvc)

set(DAS_SOURCES 
	zzip/dir.c
	zzip/err.c
	zzip/file.c
	zzip/info.c
	zzip/plugin.c
	zzip/stat.c
	zzip/zip.c
	)

add_library(zzip ${DAS_SOURCES})
