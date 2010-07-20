# Android platform definition
# Only defined compiler flags since only static libraries are supported

# Define the following variables in the toolchain file:
#   CMAKE_ANDROID_NDK_ROOT to NDK root directory
#   CMAKE_ANDROID_PLATFORM to android version, e.g. android-3
#   CMAKE_ANDROID_ARCH_ABI to armeabi or armeabi-v7a
#   CMAKE_ANDROID_NEON to 1 if neon support required
#   CMAKE_ANDROID_THUMB to 1 to compile in thumb mode
#   CMAKE_ANDROID_DEBUG to 1 to compile in debug mode

set(UNIX 1)
set(ANDROID 1)

set(CMAKE_ANDROID_SYSROOT "${CMAKE_ANDROID_NDK_ROOT}/build/platforms/${CMAKE_ANDROID_PLATFORM}/arch-arm")

set(CMAKE_C_FLAGS_INIT "-I${CMAKE_ANDROID_SYSROOT}/usr/include -fpic -mthumb-interwork -ffunction-sections -funwind-tables -fstack-protector -fno-short-enums -D__ARM_ARCH_5__ -D__ARM_ARCH_5T__ -D__ARM_ARCH_5E__ -D__ARM_ARCH_5TE__ -Wno-psabi")

if(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -march=armv7-a -mfloat-abi=softfp -mfpu=vfp")
else(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -march=armv5te -mtune=xscale -msoft-float")
ENDIF(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")

if(CMAKE_ANDROID_NEON)
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -mfpu=neon")
endif(CMAKE_ANDROID_NEON)

if(CMAKE_ANDROID_THUMB)
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -mthumb -Os -fomit-frame-pointer -fno-strict-aliasing -finline-limit=64")
else(CMAKE_ANDROID_THUMB)
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -O2 -fomit-frame-pointer -fstrict-aliasing -funswitch-loops -finline-limit=300")
endif(CMAKE_ANDROID_THUMB)

if(CMAKE_ANDROID_DEBUG)
if(CMAKE_ANDROID_THUMB)
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -marm -fno-omit-frame-pointer")
else(CMAKE_ANDROID_THUMB)
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -fno-omit-frame-pointer -fno-strict-aliasing")
endif(CMAKE_ANDROID_THUMB)
endif(CMAKE_ANDROID_DEBUG)

set(CMAKE_CXX_FLAGS_INIT ${CMAKE_C_FLAGS_INIT})
