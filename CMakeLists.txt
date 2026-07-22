cmake_minimum_required(VERSION 3.16)

project(v4l2-camera-diagnostic VERSION 0.1.0 LANGUAGES CXX)

option(V4L2DIAG_ENABLE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGPIOD libgpiod)
pkg_check_modules(LIBMICROHTTPD REQUIRED libmicrohttpd)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

function(v4l2diag_apply_warnings target_name)
  target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  if(V4L2DIAG_ENABLE_WARNINGS_AS_ERRORS)
    target_compile_options(${target_name} PRIVATE -Werror)
  endif()
endfunction()

add_library(v4l2diag_core
  source/backend/hw/src/device_discovery.cpp
  source/backend/core/src/diagnostic_runner.cpp
  source/backend/hw/src/gpio_trigger.cpp
  source/backend/hw/src/trigger_source.cpp
  source/backend/hw/src/v4l2_controls.cpp
  source/backend/core/src/profile_registry.cpp
  source/backend/core/src/threshold_registry.cpp
  source/backend/core/src/report_writer.cpp
  source/backend/core/src/stats.cpp
  source/backend/core/src/test_registry.cpp
  source/backend/core/src/types.cpp
  source/backend/hw/src/v4l2_capture.cpp
)

target_include_directories(v4l2diag_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/source/backend/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source/backend/hw/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source/backend/web/include
)

target_include_directories(v4l2diag_core PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_core PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_core PUBLIC Threads::Threads ${JSONCPP_LIBRARIES})

if(LIBGPIOD_FOUND)
  target_include_directories(v4l2diag_core PRIVATE ${LIBGPIOD_INCLUDE_DIRS})
  target_link_directories(v4l2diag_core PRIVATE ${LIBGPIOD_LIBRARY_DIRS})
  target_link_libraries(v4l2diag_core PRIVATE gpiodcxx ${LIBGPIOD_LIBRARIES})
else()
  target_link_libraries(v4l2diag_core PRIVATE gpiodcxx gpiod)
endif()

v4l2diag_apply_warnings(v4l2diag_core)

add_executable(v4l2-camera-diagnostic source/backend/cli/src/main.cpp)
target_include_directories(v4l2-camera-diagnostic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source/backend/cli/include)
target_link_libraries(v4l2-camera-diagnostic PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2-camera-diagnostic)

add_executable(v4l2-camera-diagnostic-web
  source/backend/cli/src/web_main.cpp
  source/backend/web/src/web_server.cpp
)
target_include_directories(v4l2-camera-diagnostic-web PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/source/backend/cli/include
  ${LIBMICROHTTPD_INCLUDE_DIRS}
  ${JSONCPP_INCLUDE_DIRS}
)
target_link_directories(v4l2-camera-diagnostic-web PRIVATE ${LIBMICROHTTPD_LIBRARY_DIRS} ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2-camera-diagnostic-web PRIVATE v4l2diag_core ${LIBMICROHTTPD_LIBRARIES} ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2-camera-diagnostic-web)

enable_testing()

add_executable(v4l2diag_stats_test tests/stats_test.cpp)
target_link_libraries(v4l2diag_stats_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_stats_test)
add_test(NAME stats COMMAND v4l2diag_stats_test)

add_executable(v4l2diag_profile_registry_test tests/profile_registry_test.cpp)
target_link_libraries(v4l2diag_profile_registry_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_profile_registry_test)
add_test(NAME profile_registry COMMAND v4l2diag_profile_registry_test)

add_executable(v4l2diag_trigger_model_test tests/trigger_model_test.cpp)
target_link_libraries(v4l2diag_trigger_model_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_trigger_model_test)
add_test(NAME trigger_model COMMAND v4l2diag_trigger_model_test)

add_executable(v4l2diag_report_writer_test tests/report_writer_test.cpp)
target_link_libraries(v4l2diag_report_writer_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_report_writer_test)
add_test(NAME report_writer COMMAND v4l2diag_report_writer_test)

install(TARGETS v4l2-camera-diagnostic v4l2-camera-diagnostic-web RUNTIME DESTINATION bin)
install(DIRECTORY source/backend/core/include/ DESTINATION include)
install(DIRECTORY source/backend/hw/include/ DESTINATION include)
install(DIRECTORY source/backend/web/include/ DESTINATION include)
install(DIRECTORY docs/ DESTINATION share/v4l2-camera-diagnostic/docs)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/source/frontend/dist")
  install(DIRECTORY source/frontend/dist/ DESTINATION share/v4l2-camera-diagnostic/web)
endif()
