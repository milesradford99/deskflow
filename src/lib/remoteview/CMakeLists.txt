# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

# Remote view: frame format, tile codec and H.264 (the platform's hardware codec where there is one)

set(REMOTEVIEW_SOURCES
  RemoteFrame.cpp
  RemoteFrame.h
  ScreenCapturer.h
  StreamEncoder.cpp
  StreamEncoder.h
  VideoCodec.h
  VideoPixels.cpp
)

if(APPLE)
  list(APPEND REMOTEVIEW_SOURCES VideoCodecApple.mm ScreenCapturerApple.mm)
elseif(WIN32)
  list(APPEND REMOTEVIEW_SOURCES VideoCodecWindows.cpp ScreenCapturerWindows.cpp)
else()
  if(BUILD_X11_SUPPORT)
    list(APPEND REMOTEVIEW_SOURCES ScreenCapturerX11.cpp)
  else()
    list(APPEND REMOTEVIEW_SOURCES ScreenCapturerNone.cpp)
  endif()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavcodec libavutil)
  endif()
  if(FFMPEG_FOUND)
    message(STATUS "remote view H.264: FFmpeg ${FFMPEG_libavcodec_VERSION}")
    list(APPEND REMOTEVIEW_SOURCES VideoCodecFFmpeg.cpp)
  else()
    message(STATUS "remote view H.264: not available (install libavcodec-dev), tiles only")
    list(APPEND REMOTEVIEW_SOURCES VideoCodecNone.cpp)
  endif()
endif()

add_library(remoteview STATIC ${REMOTEVIEW_SOURCES})
target_link_libraries(remoteview PUBLIC common Qt6::Gui)

if(APPLE)
  find_library(VIDEOTOOLBOX_LIBRARY VideoToolbox)
  find_library(COREMEDIA_LIBRARY CoreMedia)
  find_library(COREVIDEO_LIBRARY CoreVideo)
  find_library(COREFOUNDATION_LIBRARY CoreFoundation)
  target_link_libraries(remoteview PRIVATE ${VIDEOTOOLBOX_LIBRARY} ${COREMEDIA_LIBRARY} ${COREVIDEO_LIBRARY}
                                           ${COREFOUNDATION_LIBRARY})
  # ScreenCaptureKit only exists from macOS 12.3; the Intel build still starts on 12.0
  target_link_libraries(remoteview PUBLIC "-weak_framework ScreenCaptureKit")
elseif(WIN32)
  target_link_libraries(remoteview PRIVATE mfplat mfuuid mf wmcodecdspuuid ole32 strmiids)
else()
  if(FFMPEG_FOUND)
    target_link_libraries(remoteview PRIVATE PkgConfig::FFMPEG)
  endif()
  if(BUILD_X11_SUPPORT)
    target_link_libraries(remoteview PRIVATE ${libs})
    if(HAVE_Xinerama)
      target_compile_definitions(remoteview PRIVATE HAVE_XINERAMA=1)
    endif()
  endif()
endif()
