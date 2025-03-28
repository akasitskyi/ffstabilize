cmake_minimum_required(VERSION 3.28)
project( ffstabilize )

set (CMAKE_CXX_STANDARD 20)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(UNIX)
    if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpu=neon")
    else()
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
    endif()
endif()

SET(FFMPEG ${CMAKE_SOURCE_DIR}/ffmpeg-master-latest-win64-gpl-shared)

include_directories( c4 )
include_directories( ${FFMPEG}/include )

add_subdirectory( c4/c4 )

add_executable( ffstabilize ffstabilize.cpp )

target_link_libraries( ffstabilize
	${FFMPEG}/lib/avcodec.lib
	${FFMPEG}/lib/avformat.lib
	${FFMPEG}/lib/avutil.lib
)

if (MSVC)
	add_custom_command(TARGET ffstabilize POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG}/bin/avcodec-61.dll" $<TARGET_FILE_DIR:ffstabilize>/..)
	add_custom_command(TARGET ffstabilize POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG}/bin/avformat-61.dll" $<TARGET_FILE_DIR:ffstabilize>/..)
	add_custom_command(TARGET ffstabilize POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG}/bin/avutil-59.dll" $<TARGET_FILE_DIR:ffstabilize>/..)
	add_custom_command(TARGET ffstabilize POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG}/bin/swresample-5.dll" $<TARGET_FILE_DIR:ffstabilize>/..)
endif()

add_executable( ffstabilize_tester ffstabilize_tester.cpp )

add_dependencies( ffstabilize_tester ffstabilize )
