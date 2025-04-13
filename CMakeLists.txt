cmake_minimum_required(VERSION 3.28)
project( ffstabilize VERSION 0.0.0)

set (CMAKE_CXX_STANDARD 20)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CPACK_PACKAGE_VENDOR "Alex Kasitskyi")

add_definitions( -DPROJECT_VERSION="${CMAKE_PROJECT_VERSION}" )
add_definitions( -DPROJECT_VENDOR="${CPACK_PACKAGE_VENDOR}" )

if(UNIX)
    if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpu=neon")
    else()
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
    endif()
endif()

if (MSVC)
	SET(FFMPEG ${CMAKE_SOURCE_DIR}/ffmpeg-master-latest-win64-gpl-shared)
else()
	SET(FFMPEG ${CMAKE_SOURCE_DIR}/ffmpeg-master-latest-linux64-gpl-shared)
endif()

include_directories( c4 )
include_directories( ${FFMPEG}/include )

add_subdirectory( c4/c4 )

add_executable( ffstabilize ffstabilize.cpp )

if (MSVC)
	target_link_libraries( ffstabilize
		${FFMPEG}/lib/avcodec.lib
		${FFMPEG}/lib/avformat.lib
		${FFMPEG}/lib/avutil.lib
		${FFMPEG}/lib/swscale.lib
	)

	add_custom_command(TARGET ffstabilize POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG}/bin/avcodec-61.dll" $<TARGET_FILE_DIR:ffstabilize>/..)
	add_custom_command(TARGET ffstabilize POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG}/bin/avformat-61.dll" $<TARGET_FILE_DIR:ffstabilize>/..)
	add_custom_command(TARGET ffstabilize POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG}/bin/avutil-59.dll" $<TARGET_FILE_DIR:ffstabilize>/..)
	add_custom_command(TARGET ffstabilize POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG}/bin/swresample-5.dll" $<TARGET_FILE_DIR:ffstabilize>/..)
	add_custom_command(TARGET ffstabilize POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${FFMPEG}/bin/swscale-8.dll" $<TARGET_FILE_DIR:ffstabilize>/..)
else()
	target_link_libraries( ffstabilize
		${FFMPEG}/lib/libavcodec.so
		${FFMPEG}/lib/libavformat.so
		${FFMPEG}/lib/libavutil.so
		${FFMPEG}/lib/libswresample.so
		${FFMPEG}/lib/libswscale.so
	)
endif()

add_executable( ffstabilize_tester ffstabilize_tester.cpp )

add_dependencies( ffstabilize_tester ffstabilize )

if (MSVC)
	install(TARGETS ffstabilize DESTINATION .)
	install(FILES
		"${FFMPEG}/bin/avcodec-61.dll"
		"${FFMPEG}/bin/avformat-61.dll"
		"${FFMPEG}/bin/avutil-59.dll"
		"${FFMPEG}/bin/swresample-5.dll"
		"${FFMPEG}/bin/swscale-8.dll"
		"${FFMPEG}/LICENSE.txt"
	DESTINATION .)
else()
	install(TARGETS ffstabilize DESTINATION bin)
	install(PROGRAMS "${FFMPEG}/ffstabilize" DESTINATION .)
	install(DIRECTORY "${FFMPEG}/lib" DESTINATION .)
	install(FILES "${FFMPEG}/LICENSE.txt" DESTINATION .)
endif()

set(CPACK_WIX_VERSION 4)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ffstabilize")
set(CPACK_RESOURCE_FILE_LICENSE "${FFMPEG}/LICENSE.txt")
set(CPACK_WIX_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/wix_patch.xml")
set(CPACK_WIX_UPGRADE_GUID "A01F6B83-899C-43F3-84B7-BC820D48C953")

if (MSVC)
	set(CPACK_GENERATOR "ZIP;WIX")
else()
	set(CPACK_GENERATOR "ZIP")
endif()

include(CPack)
