cmake_minimum_required(VERSION 2.8)

include_directories(includes)

set(DAS_SOURCES
	src/OISEffect.cpp
	src/OISException.cpp
	src/OISForceFeedback.cpp
	src/OISInputManager.cpp
	src/OISJoyStick.cpp
	src/OISKeyboard.cpp
	src/OISObject.cpp
	)

if(WIN32)
set(DAS_SOURCES ${DAS_SOURCES}
	src/win32/Win32ForceFeedback.cpp
	src/win32/Win32InputManager.cpp
	src/win32/Win32JoyStick.cpp
	src/win32/Win32KeyBoard.cpp
	src/win32/Win32Mouse.cpp
	)
endif(WIN32)

add_library(ois ${DAS_SOURCES})
