CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(bluetooth-hfp-agent C)

SET(SRCS bluetooth-hfp-agent.c)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

INCLUDE(FindPkgConfig)
pkg_check_modules(pkgs_hfp_agent
		REQUIRED
		dbus-glib-1 vconf appsvc contacts-service2 tapi capi-appfw-application aul)

FOREACH(flag ${pkgs_hfp_agent_CFLAGS})
	SET(EXTRA_CFLAGS "${EXTRA_CFLAGS} ${flag}")
ENDFOREACH(flag)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EXTRA_CFLAGS}")

FIND_PROGRAM(DBUS_BINDING_TOOL NAMES dbus-binding-tool)
EXEC_PROGRAM("${DBUS_BINDING_TOOL}"
		ARGS "--prefix=bt_hfp_agent \\
		${CMAKE_CURRENT_SOURCE_DIR}/hfp_agent.xml \\
		--mode=glib-server \\
		--output=${CMAKE_CURRENT_SOURCE_DIR}/bluetooth_hfp_agent_glue.h")

ADD_EXECUTABLE(${PROJECT_NAME} ${SRCS})
TARGET_LINK_LIBRARIES(${PROJECT_NAME} ${pkgs_hfp_agent_LDFLAGS})

INSTALL(TARGETS ${PROJECT_NAME} DESTINATION bin)
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/org.bluez.hfp_agent.service
		DESTINATION share/dbus-1/services)
