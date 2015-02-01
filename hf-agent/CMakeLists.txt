CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(bluetooth-hf-agent C)

SET(SRCS bluetooth-hf-agent.c)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

IF(FEATURE_TIZENW)
	ADD_DEFINITIONS("-DFEATURE_TIZENW")
ENDIF(FEATURE_TIZENW)

INCLUDE(FindPkgConfig)
pkg_check_modules(pkgs_hf_agent
		REQUIRED
		dlog aul bluetooth-api alarm-service capi-appfw-app-manager
		syspopup-caller glib-2.0 gio-2.0 gio-unix-2.0
		deviced vconf)

FOREACH(flag ${pkgs_hf_agent_CFLAGS})
	SET(EXTRA_CFLAGS "${EXTRA_CFLAGS} ${flag}")
ENDFOREACH(flag)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EXTRA_CFLAGS}")

ADD_EXECUTABLE(${PROJECT_NAME} ${SRCS})
TARGET_LINK_LIBRARIES(${PROJECT_NAME} ${pkgs_hf_agent_LDFLAGS})

INSTALL(TARGETS ${PROJECT_NAME} DESTINATION bin)
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/org.bluez.hf_agent.service
		DESTINATION share/dbus-1/services)
