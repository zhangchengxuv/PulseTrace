# Single source for app, resources, update comparison and package/installer names.
set(PULSETRACE_VERSION "0.7.1")
if(NOT PULSETRACE_VERSION MATCHES "^([0-9]+)[.]([0-9]+)[.]([0-9]+)(-([0-9A-Za-z.-]+))?$")
  message(FATAL_ERROR "Invalid PulseTrace version")
endif()
set(PULSETRACE_VERSION_BASE "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
set(PULSETRACE_VERSION_RESOURCE "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
