CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

ADD_SUBDIRECTORY(map-agent)

ADD_SUBDIRECTORY(pb-agent)

ADD_SUBDIRECTORY(hfp-agent)

# install rule file
INSTALL(FILES ${CMAKE_BINARY_DIR}/bluetooth-agent.rule DESTINATION /opt/etc/smack/accesses.d)
