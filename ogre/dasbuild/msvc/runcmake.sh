cmake ../.. \
    -DFREETYPE_INC_SEARCH_PATH=$PWD/../../../freetype/include \
    -DFREETYPE_LIB_SEARCH_PATH=$PWD/../../../freetype/dasbuild/msvc \
    -DFreeImage_INC_SEARCH_PATH=$PWD/../../../freeimage/Source \
    -DFreeImage_LIB_SEARCH_PATH=$PWD/../../../freeimage/dasbuild/msvc \
    -DZLIB_INC_SEARCH_PATH=$PWD/../../../zlib \
    -DZLIB_LIB_SEARCH_PATH=$PWD/../../../zlib/dasbuild/msvc