# CMake 3.19 has automatic Qt target finalisation at the end of the scope.
# CMake 3.21 has RUNTIME_DEPENDENCY_SET.
cmake_minimum_required(VERSION 3.21)

project(north VERSION 1.0 DESCRIPTION "TOTP Authenticator" LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra>")
add_compile_options("$<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra>")

# We know we need at least 6.7 because of Qt SVG features.
find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets Svg Xml)

qt_add_executable(gen-icon gen-icon.cpp)
target_link_libraries(gen-icon PRIVATE Qt6::Svg Qt6::Xml)

add_custom_command(OUTPUT north.svg
	COMMAND gen-icon --kind generic > north.svg
	DEPENDS gen-icon
	COMMENT "Generating north.svg" VERBATIM)
add_custom_command(OUTPUT north-small.svg
	COMMAND gen-icon --kind small > north-small.svg
	DEPENDS gen-icon
	COMMENT "Generating north-small.svg" VERBATIM)
add_custom_command(OUTPUT north-symbolic.svg
	COMMAND gen-icon --kind symbolic > north-symbolic.svg
	DEPENDS gen-icon
	COMMENT "Generating north-symbolic.svg" VERBATIM)

set(systray_icon north-small.svg)
set(extra_sources)

if (WIN32)
	find_program(ICOTOOL_EXECUTABLE icotool REQUIRED)
	add_custom_command(
		OUTPUT north.16.png north.32.png north.48.png north.256.png north.ico
		COMMAND gen-icon --png 16  > north.16.png
		COMMAND gen-icon --png 32  > north.32.png
		COMMAND gen-icon --png 48  > north.48.png
		COMMAND gen-icon --png 256 > north.256.png
		COMMAND ${ICOTOOL_EXECUTABLE} -c -o north.ico --raw north.256.png
			north.16.png north.32.png north.48.png
		DEPENDS gen-icon
		COMMENT "Generating north.ico" VERBATIM)

	# QT_TARGET_* are undocumented and wouldn't set the dependency.
	set(resource_file "${PROJECT_BINARY_DIR}/north.rc")
	list(APPEND extra_sources "${resource_file}")
	add_custom_command(OUTPUT "${resource_file}"
		COMMAND ${CMAKE_COMMAND} -E echo "1 ICON \"north.ico\""
			> "${resource_file}" VERBATIM)
	set_property(SOURCE "${resource_file}"
		APPEND PROPERTY OBJECT_DEPENDS "${PROJECT_BINARY_DIR}/north.ico")
elseif (APPLE)
	set(systray_icon north-symbolic.svg)
	list(APPEND extra_sources north.icns)

	find_program(ICONUTIL_EXECUTABLE iconutil REQUIRED)
	add_custom_command(
		OUTPUT north.icns
		COMMAND ${CMAKE_COMMAND} -E make_directory north.iconset
		COMMAND gen-icon -k mac -p 16   > north.iconset/icon_16x16.png
		COMMAND gen-icon -k mac -p 32   > north.iconset/icon_16x16@2x.png
		COMMAND gen-icon -k mac -p 32   > north.iconset/icon_32x32.png
		COMMAND gen-icon -k mac -p 64   > north.iconset/icon_32x32@2x.png
		COMMAND gen-icon -k mac -p 64   > north.iconset/icon_64x64.png
		COMMAND gen-icon -k mac -p 128  > north.iconset/icon_64x64@2x.png
		COMMAND gen-icon -k mac -p 128  > north.iconset/icon_128x128.png
		COMMAND gen-icon -k mac -p 256  > north.iconset/icon_128x128@2x.png
		COMMAND gen-icon -k mac -p 256  > north.iconset/icon_256x256.png
		COMMAND gen-icon -k mac -p 512  > north.iconset/icon_256x256@2x.png
		COMMAND gen-icon -k mac -p 512  > north.iconset/icon_512x512.png
		COMMAND gen-icon -k mac -p 1024 > north.iconset/icon_512x512@2x.png
		COMMAND ${ICONUTIL_EXECUTABLE} -c icns -o north.icns north.iconset
		COMMAND ${CMAKE_COMMAND} -E remove_directory north.iconset
		DEPENDS gen-icon
		COMMENT "Generating north.icns" VERBATIM)
	set_source_files_properties(north.icns PROPERTIES
		MACOSX_PACKAGE_LOCATION Resources)
else()
	# Perhaps it only makes sense to install these if normal renders suck,
	# and I use fucking Qt SVG here, a very poor renderer.
	set(icons north.svg)
	foreach (size 16 22 24 32 48 64 128 256)
		set(dir icons/hicolor/${size}x${size}/apps)
		set(path "${dir}/north.png")
		add_custom_command(OUTPUT "${path}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}"
			COMMAND gen-icon --png ${size} > "${path}"
			DEPENDS gen-icon
			COMMENT "Generating ${size}x${size} north.png" VERBATIM)
		list(APPEND icons "${path}")
	endforeach()
	add_custom_target(icons ALL DEPENDS ${icons})

	include(GNUInstallDirs)
	install(DIRECTORY "${PROJECT_BINARY_DIR}/icons"
		DESTINATION ${CMAKE_INSTALL_DATADIR})
	install(FILES "${PROJECT_BINARY_DIR}/north.svg"
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

set_property(SOURCE ${systray_icon}
	APPEND PROPERTY QT_RESOURCE_ALIAS systray.svg)
set_property(SOURCE edit-copy-symbolic.svg
	APPEND PROPERTY QT_RESOURCE_ALIAS edit-copy-symbolic.svg)

qt_add_executable(north north.cpp north.h ${extra_sources})
qt_add_resources(north "rsrc" PREFIX / FILES
	${PROJECT_BINARY_DIR}/${systray_icon} edit-copy-symbolic.svg)
target_link_libraries(north PRIVATE Qt6::Widgets)
if (APPLE)
	target_link_libraries(north PRIVATE "-framework ApplicationServices")
endif()

set_target_properties(north PROPERTIES
	MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/Info.plist.in
	MACOSX_BUNDLE_GUI_IDENTIFIER name.janouch.north
	MACOSX_BUNDLE_ICON_FILE north.icns
	MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
	MACOSX_BUNDLE_SHORT_VERSION_STRING
		${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
	MACOSX_BUNDLE TRUE
	WIN32_EXECUTABLE TRUE)

include(GNUInstallDirs)
install(TARGETS north
	RUNTIME_DEPENDENCY_SET dependencies
	BUNDLE DESTINATION .
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if (WIN32)
	install(RUNTIME_DEPENDENCY_SET dependencies DIRECTORIES $ENV{PATH}
		PRE_EXCLUDE_REGEXES "api-ms-win-.*"
		POST_EXCLUDE_REGEXES ".*[/\\]system32[/\\].*")
endif()
if (NOT WIN32 AND NOT APPLE)
	install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
	install(FILES north.desktop
		DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
elseif (QT6_IS_SHARED_LIBS_BUILD)
	# Note that under MSYS2, it suffices to install qt6-static,
	# and then prepend its subprefix to PATH before invoking cmake.
	# To keep size low, remember to strip the resulting binary.

	qt_generate_deploy_app_script(TARGET north OUTPUT_SCRIPT deploy)
	install(SCRIPT "${deploy}")

	# At least with Homebrew Qt, you will get QTBUG-127075,
	# which can be circumvented by running macdeployqt twice.
	if (APPLE)
		install(SCRIPT "${deploy}")
	endif()
endif()

# CPack
set(CPACK_PACKAGE_VENDOR "Premysl Eric Janouch")
set(CPACK_PACKAGE_CONTACT "Přemysl Eric Janouch <p@janouch.name>")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_PACKAGE_FILE_NAME
	"${PROJECT_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME} ${PROJECT_VERSION}")
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_SOURCE_IGNORE_FILES "/build;/CMakeLists.txt.user")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}")

include(CPack)
