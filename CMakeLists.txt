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
  source/backend/core/src/config_migration.cpp
  source/backend/core/src/profile_registry.cpp
  source/backend/core/src/threshold_registry.cpp
  source/backend/core/src/report_writer.cpp
  source/backend/core/src/run_config_json.cpp
  source/backend/core/src/run_status.cpp
  source/backend/core/src/stats.cpp
  source/backend/core/src/test_json.cpp
  source/backend/core/src/test_registry.cpp
  source/backend/core/src/role_bindings.cpp
  source/backend/core/src/duration_format.cpp
  source/backend/core/src/result_card.cpp
  source/backend/core/src/test_content.cpp
  source/backend/core/src/report_naming.cpp
  source/backend/core/src/run_result_json.cpp
  source/backend/core/src/run_routing.cpp
  source/backend/core/src/trigger_registry.cpp
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

add_executable(v4l2diag_test_selection_test tests/test_selection_test.cpp)
target_link_libraries(v4l2diag_test_selection_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_test_selection_test)
add_test(NAME test_selection COMMAND v4l2diag_test_selection_test)

add_executable(v4l2diag_report_writer_test tests/report_writer_test.cpp)
# Parses the JSON report rather than substring-searching it, so it needs jsoncpp.
target_include_directories(v4l2diag_report_writer_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_report_writer_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_report_writer_test PRIVATE v4l2diag_core ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_report_writer_test)
add_test(NAME report_writer COMMAND v4l2diag_report_writer_test)

add_executable(v4l2diag_format_enumeration_test tests/format_enumeration_test.cpp)
target_link_libraries(v4l2diag_format_enumeration_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_format_enumeration_test)
add_test(NAME format_enumeration COMMAND v4l2diag_format_enumeration_test)

add_executable(v4l2diag_gpio_trigger_worker_test tests/gpio_trigger_worker_test.cpp)
target_link_libraries(v4l2diag_gpio_trigger_worker_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_gpio_trigger_worker_test)
add_test(NAME gpio_trigger_worker COMMAND v4l2diag_gpio_trigger_worker_test)

add_executable(v4l2diag_threshold_registry_test tests/threshold_registry_test.cpp)
target_link_libraries(v4l2diag_threshold_registry_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_threshold_registry_test)
add_test(NAME threshold_registry COMMAND v4l2diag_threshold_registry_test)

add_executable(v4l2diag_frame_rate_test tests/frame_rate_test.cpp)
target_link_libraries(v4l2diag_frame_rate_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_frame_rate_test)
add_test(NAME frame_rate COMMAND v4l2diag_frame_rate_test)

add_executable(v4l2diag_pacing_test tests/pacing_test.cpp)
target_link_libraries(v4l2diag_pacing_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_pacing_test)
add_test(NAME pacing COMMAND v4l2diag_pacing_test)

# Drives the real HTTP API, so it needs the web server sources and microhttpd.
add_executable(v4l2diag_web_profiles_api_test
  tests/web_profiles_api_test.cpp
  source/backend/web/src/web_server.cpp
)
target_include_directories(v4l2diag_web_profiles_api_test PRIVATE ${LIBMICROHTTPD_INCLUDE_DIRS} ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_web_profiles_api_test PRIVATE ${LIBMICROHTTPD_LIBRARY_DIRS} ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_web_profiles_api_test PRIVATE v4l2diag_core ${LIBMICROHTTPD_LIBRARIES} ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_web_profiles_api_test)
add_test(NAME web_profiles_api COMMAND v4l2diag_web_profiles_api_test)

add_executable(v4l2diag_config_migration_test tests/config_migration_test.cpp)
target_link_libraries(v4l2diag_config_migration_test PRIVATE v4l2diag_core)
target_include_directories(v4l2diag_config_migration_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_config_migration_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_config_migration_test PRIVATE ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_config_migration_test)
add_test(NAME config_migration COMMAND v4l2diag_config_migration_test)

add_executable(v4l2diag_test_registry_metadata_test tests/test_registry_metadata_test.cpp)
target_link_libraries(v4l2diag_test_registry_metadata_test PRIVATE v4l2diag_core)
target_include_directories(v4l2diag_test_registry_metadata_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_test_registry_metadata_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_test_registry_metadata_test PRIVATE ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_test_registry_metadata_test)
add_test(NAME test_registry_metadata COMMAND v4l2diag_test_registry_metadata_test)

add_executable(v4l2diag_report_formats_test tests/report_formats_test.cpp)
target_link_libraries(v4l2diag_report_formats_test PRIVATE v4l2diag_core)
target_include_directories(v4l2diag_report_formats_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_report_formats_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_report_formats_test PRIVATE ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_report_formats_test)
add_test(NAME report_formats COMMAND v4l2diag_report_formats_test)

add_executable(v4l2diag_run_config_json_test tests/run_config_json_test.cpp)
target_link_libraries(v4l2diag_run_config_json_test PRIVATE v4l2diag_core)
target_include_directories(v4l2diag_run_config_json_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_run_config_json_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_run_config_json_test PRIVATE ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_run_config_json_test)
add_test(NAME run_config_json COMMAND v4l2diag_run_config_json_test)

add_executable(v4l2diag_run_status_test tests/run_status_test.cpp)
target_link_libraries(v4l2diag_run_status_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_run_status_test)
add_test(NAME run_status COMMAND v4l2diag_run_status_test)

add_executable(v4l2diag_verdict_rules_test tests/verdict_rules_test.cpp)
target_link_libraries(v4l2diag_verdict_rules_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_verdict_rules_test)
add_test(NAME verdict_rules COMMAND v4l2diag_verdict_rules_test)

# Drives the real CLI binary, so the argument contract itself is under test.
add_executable(v4l2diag_cli_role_binding_test tests/cli_role_binding_test.cpp)
target_compile_definitions(v4l2diag_cli_role_binding_test PRIVATE
  V4L2DIAG_CLI_BINARY="$<TARGET_FILE:v4l2-camera-diagnostic>")
add_dependencies(v4l2diag_cli_role_binding_test v4l2-camera-diagnostic)
v4l2diag_apply_warnings(v4l2diag_cli_role_binding_test)
add_test(NAME cli_role_binding COMMAND v4l2diag_cli_role_binding_test)

add_executable(v4l2diag_report_basename_test tests/report_basename_test.cpp)
target_include_directories(v4l2diag_report_basename_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_report_basename_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_report_basename_test PRIVATE v4l2diag_core ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_report_basename_test)
add_test(NAME report_basename COMMAND v4l2diag_report_basename_test)

add_executable(v4l2diag_test_content_registry_test tests/test_content_registry_test.cpp)
target_link_libraries(v4l2diag_test_content_registry_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_test_content_registry_test)
add_test(NAME test_content_registry COMMAND v4l2diag_test_content_registry_test)

add_executable(v4l2diag_report_dom_audit_test tests/report_dom_audit_test.cpp)
target_include_directories(v4l2diag_report_dom_audit_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_report_dom_audit_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_report_dom_audit_test PRIVATE v4l2diag_core ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_report_dom_audit_test)
add_test(NAME report_dom_audit COMMAND v4l2diag_report_dom_audit_test)

add_executable(v4l2diag_report_card_contract_test tests/report_card_contract_test.cpp)
target_include_directories(v4l2diag_report_card_contract_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_report_card_contract_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_report_card_contract_test PRIVATE v4l2diag_core ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_report_card_contract_test)
add_test(NAME report_card_contract COMMAND v4l2diag_report_card_contract_test)

add_executable(v4l2diag_report_css_contract_test tests/report_css_contract_test.cpp)
target_include_directories(v4l2diag_report_css_contract_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_report_css_contract_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_report_css_contract_test PRIVATE v4l2diag_core ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_report_css_contract_test)
add_test(NAME report_css_contract COMMAND v4l2diag_report_css_contract_test)

add_executable(v4l2diag_metric_name_contract_test tests/metric_name_contract_test.cpp)
v4l2diag_apply_warnings(v4l2diag_metric_name_contract_test)
# Reads the two source files, so it must run from the repository root.
add_test(NAME metric_name_contract COMMAND v4l2diag_metric_name_contract_test
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(v4l2diag_preview_structure_test tests/preview_structure_test.cpp)
target_link_libraries(v4l2diag_preview_structure_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_preview_structure_test)
# Reads docs/assets/refactored_previews/, so it must run from the repository root.
add_test(NAME preview_structure COMMAND v4l2diag_preview_structure_test
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(v4l2diag_no_fabricated_values_test tests/no_fabricated_values_test.cpp)
v4l2diag_apply_warnings(v4l2diag_no_fabricated_values_test)
# Reads test_content.cpp, so it must run from the repository root.
add_test(NAME no_fabricated_values COMMAND v4l2diag_no_fabricated_values_test
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(v4l2diag_run_metric_coverage_test tests/run_metric_coverage_test.cpp)
v4l2diag_apply_warnings(v4l2diag_run_metric_coverage_test)
# Reads test_content.cpp and the recorded device run, so it must run from the root.
add_test(NAME run_metric_coverage COMMAND v4l2diag_run_metric_coverage_test
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(v4l2diag_export_toolbar_test tests/export_toolbar_test.cpp)
target_include_directories(v4l2diag_export_toolbar_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_export_toolbar_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_export_toolbar_test PRIVATE v4l2diag_core ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_export_toolbar_test)
add_test(NAME export_toolbar COMMAND v4l2diag_export_toolbar_test)

add_executable(v4l2diag_result_card_template_test tests/result_card_template_test.cpp)
target_include_directories(v4l2diag_result_card_template_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_result_card_template_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_result_card_template_test PRIVATE v4l2diag_core ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_result_card_template_test)
add_test(NAME result_card_template COMMAND v4l2diag_result_card_template_test)

add_executable(v4l2diag_duration_format_test tests/duration_format_test.cpp)
target_link_libraries(v4l2diag_duration_format_test PRIVATE v4l2diag_core)
# The vectors are shared with the frontend test; both read this one file.
target_compile_definitions(v4l2diag_duration_format_test PRIVATE
  V4L2DIAG_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
v4l2diag_apply_warnings(v4l2diag_duration_format_test)
add_test(NAME duration_format COMMAND v4l2diag_duration_format_test)

add_executable(v4l2diag_wire_contract_test tests/wire_contract_test.cpp)
target_include_directories(v4l2diag_wire_contract_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_wire_contract_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_wire_contract_test PRIVATE v4l2diag_core ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_wire_contract_test)
add_test(NAME wire_contract COMMAND v4l2diag_wire_contract_test)

add_executable(v4l2diag_run_result_json_test tests/run_result_json_test.cpp)
target_include_directories(v4l2diag_run_result_json_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_run_result_json_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_run_result_json_test PRIVATE v4l2diag_core ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_run_result_json_test)
add_test(NAME run_result_json COMMAND v4l2diag_run_result_json_test)

add_executable(v4l2diag_report_formats_removal_test tests/report_formats_removal_test.cpp)
target_include_directories(v4l2diag_report_formats_removal_test PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_link_directories(v4l2diag_report_formats_removal_test PRIVATE ${JSONCPP_LIBRARY_DIRS})
target_link_libraries(v4l2diag_report_formats_removal_test PRIVATE v4l2diag_core ${JSONCPP_LIBRARIES})
v4l2diag_apply_warnings(v4l2diag_report_formats_removal_test)
add_test(NAME report_formats_removal COMMAND v4l2diag_report_formats_removal_test)

add_executable(v4l2diag_run_routing_test tests/run_routing_test.cpp)
target_link_libraries(v4l2diag_run_routing_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_run_routing_test)
add_test(NAME run_routing COMMAND v4l2diag_run_routing_test)

add_executable(v4l2diag_role_bindings_test tests/role_bindings_test.cpp)
target_link_libraries(v4l2diag_role_bindings_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_role_bindings_test)
add_test(NAME role_bindings COMMAND v4l2diag_role_bindings_test)

add_executable(v4l2diag_trigger_registry_test tests/trigger_registry_test.cpp)
target_link_libraries(v4l2diag_trigger_registry_test PRIVATE v4l2diag_core)
v4l2diag_apply_warnings(v4l2diag_trigger_registry_test)
add_test(NAME trigger_registry COMMAND v4l2diag_trigger_registry_test)

install(TARGETS v4l2-camera-diagnostic v4l2-camera-diagnostic-web RUNTIME DESTINATION bin)
install(DIRECTORY source/backend/core/include/ DESTINATION include)
install(DIRECTORY source/backend/hw/include/ DESTINATION include)
install(DIRECTORY source/backend/web/include/ DESTINATION include)
install(DIRECTORY docs/ DESTINATION share/v4l2-camera-diagnostic/docs)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/source/frontend/dist")
  install(DIRECTORY source/frontend/dist/ DESTINATION share/v4l2-camera-diagnostic/web)
endif()
