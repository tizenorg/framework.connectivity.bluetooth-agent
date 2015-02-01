CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(bluetooth-pb-agent C)

SET(SRCS bluetooth_pb_agent.c bluetooth_pb_vcard.c)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

INCLUDE(FindPkgConfig)
pkg_check_modules(pkgs_pb_agent
		REQUIRED
		dbus-glib-1 dlog contacts-service2 tapi vconf capi-media-image-util libexif)

FOREACH(flag ${pkgs_pb_agent_CFLAGS})
	SET(EXTRA_CFLAGS "${EXTRA_CFLAGS} ${flag}")
ENDFOREACH(flag)
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EXTRA_CFLAGS}")

FIND_PROGRAM(DBUS_BINDING_TOOL NAMES dbus-binding-tool)
EXEC_PROGRAM("${DBUS_BINDING_TOOL}"
		ARGS "--prefix=bluetooth_pb \\
		${CMAKE_CURRENT_SOURCE_DIR}/bluetooth_pb_agent.xml \\
		--mode=glib-server \\
		--output=${CMAKE_CURRENT_SOURCE_DIR}/bluetooth_pb_agent_glue.h")

ADD_EXECUTABLE(${PROJECT_NAME} ${SRCS})
TARGET_LINK_LIBRARIES(${PROJECT_NAME} ${pkgs_pb_agent_LDFLAGS})

INSTALL(TARGETS ${PROJECT_NAME} DESTINATION bin)
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/org.bluez.pb_agent.service
		DESTINATION share/dbus-1/services)
