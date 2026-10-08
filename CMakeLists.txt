cmake_minimum_required(VERSION 3.24)
project(ERShorts VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT WIN32)
  message(FATAL_ERROR "ERShorts는 Windows 전용입니다 (game_capture, D3D11, WASAPI 사용).")
endif()

# Qt6: OBS가 쓰는 것과 같은 메이저 버전(Qt6) 사용
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)

# libobs: OBS Studio 소스를 빌드 후 설치(Development 포함)하면 생기는 libobsConfig.cmake 사용
#   -Dlibobs_DIR=<설치폴더>/cmake/libobs -Dw32-pthreads_DIR=<설치폴더>/cmake/w32-pthreads
#   (자세한 순서는 .github/workflows/build-windows.yml 참고)
find_package(libobs REQUIRED)

add_executable(ERShorts WIN32
  src/main.cpp
  src/MainWindow.h        src/MainWindow.cpp
  src/ObsCore.h           src/ObsCore.cpp
  src/PreviewWidget.h     src/PreviewWidget.cpp
  src/GlobalHotkey.h      src/GlobalHotkey.cpp
  src/ShortsExporter.h    src/ShortsExporter.cpp
  src/EditProject.h       src/EditProject.cpp
  src/TimelineWidget.h    src/TimelineWidget.cpp
  src/EditorWindow.h      src/EditorWindow.cpp
  src/BeatDetector.h      src/BeatDetector.cpp
  src/ThumbnailCache.h    src/ThumbnailCache.cpp
  src/Diagnostics.h       src/Diagnostics.cpp
  src/BandLayout.h        src/BandLayout.cpp
  src/FontManager.h       src/FontManager.cpp
  src/ClipInfoCache.h     src/ClipInfoCache.cpp
  src/StylePresets.h      src/StylePresets.cpp
  src/AudioEnvelope.h     src/AudioEnvelope.cpp
  src/KillSoundDetector.h src/KillSoundDetector.cpp src/KillSoundTemplates.h
)

target_link_libraries(ERShorts PRIVATE
  Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets OBS::libobs user32 dbghelp)

if(MSVC)
  target_compile_options(ERShorts PRIVATE /utf-8 /W3)
endif()
# Windows.h 의 min/max 매크로가 std::min/std::max 를 망가뜨리지 않도록
target_compile_definitions(ERShorts PRIVATE NOMINMAX)

# 실행 파일을 OBS와 같은 폴더 구조(bin/64bit)에 두면
# libobs가 플러그인(../../obs-plugins/64bit)과 데이터(../../data)를 기본 경로에서 찾습니다.
# ($<0:> 는 Visual Studio가 Release/ 같은 하위 폴더를 붙이지 않게 하는 트릭)
set_target_properties(ERShorts PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/bin/64bit/$<0:>")

# 폴더 맨 위에 두는 실행기 (더블클릭하면 bin/64bit/ERShorts.exe 실행)
add_executable(ERShortsLauncher WIN32 launcher/main.cpp)
if(MSVC)
  target_compile_options(ERShortsLauncher PRIVATE /utf-8)
endif()
target_compile_definitions(ERShortsLauncher PRIVATE UNICODE _UNICODE)
set_target_properties(ERShortsLauncher PROPERTIES
  OUTPUT_NAME "ERShorts"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/$<0:>")
