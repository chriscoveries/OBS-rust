cmake_minimum_required(VERSION 3.28...3.30)

find_package(FFmpeg 8.0 REQUIRED avcodec avdevice avutil avformat)

add_library(media-playback INTERFACE)
add_library(OBS::media-playback ALIAS media-playback)

# Unity groups (BUILD.md), compiled in each target that links this library:
# build/media-playback.<name>+.h.c #includes the implementation files
# (media-playback/foo.c.h) behind one precompiled header per group.
target_sources(
  media-playback
  INTERFACE
    build/media-playback.cache+.h.c
    build/media-playback.core+.h.c
    media-playback/cache.h
    media-playback/closest-format.h
    media-playback/decode.h
    media-playback/media-playback.h
    media-playback/media.h
    media-playback/network-timeout.h
    media-playback/platform-decode-ffmpeg.h
    media-playback/platform-media-ffmpeg.h
)

target_include_directories(media-playback INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(media-playback INTERFACE FFmpeg::avcodec FFmpeg::avdevice FFmpeg::avutil FFmpeg::avformat)
