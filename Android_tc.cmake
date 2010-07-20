include(CMakeForceCompiler)

set(CMAKE_SYSTEM_NAME Android)

set(CMAKE_ANDROID_NDK_ROOT "C:/Android/android-ndk-r4-crystax")
set(CMAKE_ANDROID_PLATFORM "android-8")
set(CMAKE_ANDROID_ARCH_ABI "armeabi-v7a")
#set(CMAKE_ANDROID_ARCH_ABI "armeabi")
#set(CMAKE_ANDROID_NEON 1)
#set(CMAKE_ANDROID_THUMB 1) 
#set(CMAKE_ANDROID_DEBUG 1)

CMAKE_FORCE_C_COMPILER(C:/Android/cs/bin/arm-none-eabi-gcc.exe GNU)
CMAKE_FORCE_CXX_COMPILER(C:/Android/cs/bin/arm-none-eabi-g++.exe GNU)

set(CMAKE_FILE_ROOT_PATH C:/Android/android-ndk-r4-crystax/build/platforms/android-8)
# search for programs in the build host directories
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# for libraries and headers in the target directories
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
