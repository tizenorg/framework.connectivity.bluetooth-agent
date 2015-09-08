CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(bluetooth-ag-agent C)

SET(SRCS bluetooth-ag-agent.c
			bluetooth-ag-handler.c
			bluetooth-ag-manager.c)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

SET(PKG_MODULES
	dlog
	dbus-glib-1
	vconf
	appsvc
	contacts-service2
	tapi
	capi-appfw-application
	aul
	capi-system-info
	glib-2.0
	gio-2.0
	gio-unix-2.0
)

INCLUDE(FindPkgConfig)
pkg_check_modules(pkgs_ag_agent REQUIRED ${PKG_MODULES})

FOREACH(flag ${pkgs_ag_agent_CFLAGS})
	SET(EXTRA_CFLAGS "${EXTRA_CFLAGS} ${flag}")
ENDFOREACH(flag)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EXTRA_CFLAGS}")

SET(APP_SYSCONFDIR /opt/var/lib/bluetooth)

ADD_DEFINITIONS("-DAPP_SYSCONFDIR=\"${APP_SYSCONFDIR}\"")

ADD_EXECUTABLE(${PROJECT_NAME} ${SRCS})
TARGET_LINK_LIBRARIES(${PROJECT_NAME} ${pkgs_ag_agent_LDFLAGS})

INSTALL(TARGETS ${PROJECT_NAME} DESTINATION bin)
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/org.bluez.ag_agent.service
		DESTINATION share/dbus-1/services)

INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/voice-recognition-blacklist
		DESTINATION /opt/var/lib/bluetooth/)
