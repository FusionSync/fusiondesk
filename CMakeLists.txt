cmake_minimum_required(VERSION 3.20)

project(fusiondesk LANGUAGES CXX)

add_library(fusiondesk_core
    src/core/diagnostics/diagnostics_sink.cpp
    src/core/module/module_catalog.cpp
    src/core/module/module_compatibility.cpp
    src/core/module/module_factory.cpp
    src/core/module/module_ingress.cpp
    src/core/module/module_host.cpp
    src/core/network/channel_allowlist.cpp
    src/core/network/channel_defaults.cpp
    src/core/network/channel_registry.cpp
    src/core/network/network_manager.cpp
    src/core/network/network_router.cpp
    src/core/network/priority_scheduler.cpp
    src/core/network/request_tracker.cpp
    src/core/network/socket_group.cpp
    src/core/policy/policy_engine.cpp
    src/core/protocol/capability_exchange.cpp
    src/core/protocol/capability_negotiation.cpp
    src/core/protocol/packet_codec.cpp
    src/core/protocol/protocol_validator.cpp
    src/core/session/session.cpp
    src/core/session/session_manager.cpp
    src/modules/clipboard/clipboard_drag_coordinates.cpp
    src/modules/clipboard/clipboard_factory.cpp
    src/modules/clipboard/clipboard_large_data_scheduler.cpp
    src/modules/clipboard/clipboard_module_drag.cpp
    src/modules/clipboard/clipboard_module_local_read.cpp
    src/modules/clipboard/clipboard_module_object_lock.cpp
    src/modules/clipboard/clipboard_module_remote_read.cpp
    src/modules/clipboard/clipboard_modules.cpp
    src/modules/clipboard/clipboard_transfer.cpp
    src/modules/clipboard/clipboard_transfer_file_list.cpp
    src/modules/clipboard/clipboard_transfer_format.cpp
    src/modules/clipboard/clipboard_transfer_registry.cpp
    src/modules/clipboard/clipboard_types.cpp
    src/modules/clipboard/fdcl_codec.cpp
    src/modules/clipboard/fdcl_codec_object_lock.cpp
    src/modules/clipboard/fdcl_encoder.cpp
    src/modules/clipboard/fdcl_model_conversion.cpp
    src/modules/display/display_factory.cpp
    src/modules/display/display_control_codec.cpp
    src/modules/display/display_encoded_video_payload.cpp
    src/modules/display/display_frame_codec.cpp
    src/modules/display/display_modules.cpp
    src/modules/feature/feature_module_factory.cpp
    src/modules/input/input_factory.cpp
    src/modules/input/input_modules.cpp
    src/modules/input/input_payload_codec.cpp
    src/modules/test/test_echo_factory.cpp
    src/modules/test/test_echo_modules.cpp
    src/runtime/connection/peer_connection_plan.cpp
    src/runtime/connection/peer_coordination.cpp
    src/runtime/connection/peer_profile_exchange.cpp
    src/runtime/connection/peer_profile_runtime_service.cpp
    src/runtime/connection/peer_profile_service.cpp
    src/runtime/connection/module_inventory_exchange.cpp
    src/runtime/connection/module_inventory_runtime_service.cpp
    src/runtime/connection/reconnect_coordinator.cpp
    src/runtime/connection/reconnect_diagnostics_report.cpp
    src/runtime/connection/reconnect_orchestration_plan.cpp
    src/runtime/connection/reconnect_runtime_service.cpp
    src/runtime/connection/reconnect_teardown_ack.cpp
    src/runtime/connection/reconnect_teardown_dispatch.cpp
    src/runtime/connection/reconnect_teardown_handler.cpp
    src/runtime/connection/reconnect_teardown_service.cpp
    src/runtime/display/display_capture_backend_factory.cpp
    src/runtime/display/display_capture_backend_failover.cpp
    src/runtime/display/display_capture_options.cpp
    src/runtime/display/display_capture_geometry.cpp
    src/runtime/display/display_color_conversion.cpp
    src/runtime/display/display_codec_backend_factory.cpp
    src/runtime/display/display_codec_negotiation.cpp
    src/runtime/display/display_codec_peer_profile.cpp
    src/runtime/display/display_codec_selection.cpp
    src/runtime/display/display_capture_platform_plan.cpp
    src/runtime/display/display_capture_recovery.cpp
    src/runtime/display/display_capture_backend_selection.cpp
    src/runtime/display/display_source_selection.cpp
    src/runtime/display/display_runtime_service.cpp
    src/runtime/feature/clipboard_product_policy.cpp
    src/runtime/feature/clipboard_product_presenter.cpp
    src/runtime/feature/clipboard_runtime_policy.cpp
    src/runtime/feature/clipboard_runtime_remote_object_lock.cpp
    src/runtime/feature/clipboard_runtime_service.cpp
    src/runtime/feature/feature_runtime_policy.cpp
    src/runtime/feature/feature_runtime_service.cpp
    src/runtime/session/link_channel_binding_report.cpp
    src/runtime/session/session_mainline.cpp
    src/runtime/session/display_product_health_presenter.cpp
    src/runtime/session/session_runtime_diagnostics.cpp
    src/runtime/tunnel/tunnel_candidate_profile.cpp
    src/runtime/tunnel/tunnel_reconnect_executor.cpp
    src/runtime/runtime_host.cpp
)

target_include_directories(fusiondesk_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(fusiondesk_core PUBLIC cxx_std_17)

enable_testing()

add_test(
    NAME fusiondesk_source_purity_scan
    COMMAND
        ${CMAKE_COMMAND}
        -DFUSIONDESK_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_source_purity.cmake
)

add_executable(fusiondesk_module_host_tests
    tests/core/module/module_host_smoke.cpp
)

target_link_libraries(fusiondesk_module_host_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_module_host_tests COMMAND fusiondesk_module_host_tests)

add_executable(fusiondesk_module_catalog_tests
    tests/core/module/module_catalog_smoke.cpp
)

target_link_libraries(fusiondesk_module_catalog_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_module_catalog_tests COMMAND fusiondesk_module_catalog_tests)

add_executable(fusiondesk_module_ingress_tests
    tests/core/module/module_ingress_smoke.cpp
)

target_link_libraries(fusiondesk_module_ingress_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_module_ingress_tests COMMAND fusiondesk_module_ingress_tests)

add_executable(fusiondesk_module_factory_tests
    tests/core/module/module_factory_smoke.cpp
)

target_link_libraries(fusiondesk_module_factory_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_module_factory_tests COMMAND fusiondesk_module_factory_tests)

add_executable(fusiondesk_module_protocol_tests
    tests/core/module/module_protocol_smoke.cpp
)

target_link_libraries(fusiondesk_module_protocol_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_module_protocol_tests COMMAND fusiondesk_module_protocol_tests)

add_executable(fusiondesk_display_mvp_tests
    tests/modules/display/display_mvp_smoke.cpp
)

target_link_libraries(fusiondesk_display_mvp_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_display_mvp_tests COMMAND fusiondesk_display_mvp_tests)

add_executable(fusiondesk_clipboard_fdcl_codec_tests
    tests/modules/clipboard/clipboard_fdcl_codec_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_fdcl_codec_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_clipboard_fdcl_codec_tests COMMAND fusiondesk_clipboard_fdcl_codec_tests)

add_executable(fusiondesk_clipboard_transfer_tests
    tests/modules/clipboard/clipboard_transfer_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_transfer_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_clipboard_transfer_tests COMMAND fusiondesk_clipboard_transfer_tests)

add_executable(fusiondesk_clipboard_large_data_scheduler_tests
    tests/modules/clipboard/clipboard_large_data_scheduler_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_large_data_scheduler_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_clipboard_large_data_scheduler_tests
    COMMAND fusiondesk_clipboard_large_data_scheduler_tests
)

add_executable(fusiondesk_clipboard_module_large_data_tests
    tests/modules/clipboard/clipboard_module_large_data_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_module_large_data_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_clipboard_module_large_data_tests
    COMMAND fusiondesk_clipboard_module_large_data_tests
)

add_executable(fusiondesk_clipboard_module_file_transfer_tests
    tests/modules/clipboard/clipboard_module_file_transfer_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_module_file_transfer_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_clipboard_module_file_transfer_tests
    COMMAND fusiondesk_clipboard_module_file_transfer_tests
)

add_executable(fusiondesk_clipboard_module_drag_tests
    tests/modules/clipboard/clipboard_module_drag_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_module_drag_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_clipboard_module_drag_tests
    COMMAND fusiondesk_clipboard_module_drag_tests
)

add_executable(fusiondesk_clipboard_drag_coordinates_tests
    tests/modules/clipboard/clipboard_drag_coordinates_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_drag_coordinates_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_clipboard_drag_coordinates_tests
    COMMAND fusiondesk_clipboard_drag_coordinates_tests
)

add_executable(fusiondesk_clipboard_module_tests
    tests/modules/clipboard/clipboard_module_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_module_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_clipboard_module_tests COMMAND fusiondesk_clipboard_module_tests)

add_executable(fusiondesk_clipboard_module_object_lock_tests
    tests/modules/clipboard/clipboard_module_object_lock_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_module_object_lock_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_clipboard_module_object_lock_tests
    COMMAND fusiondesk_clipboard_module_object_lock_tests
)

add_executable(fusiondesk_display_frame_codec_tests
    tests/modules/display/display_frame_codec_smoke.cpp
)

target_link_libraries(fusiondesk_display_frame_codec_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_display_frame_codec_tests COMMAND fusiondesk_display_frame_codec_tests)

add_executable(fusiondesk_display_encoded_video_payload_tests
    tests/modules/display/display_encoded_video_payload_smoke.cpp
)

target_link_libraries(fusiondesk_display_encoded_video_payload_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_encoded_video_payload_tests
    COMMAND fusiondesk_display_encoded_video_payload_tests)

add_executable(fusiondesk_display_color_conversion_tests
    tests/runtime/display/display_color_conversion_smoke.cpp
)

target_link_libraries(fusiondesk_display_color_conversion_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_color_conversion_tests
    COMMAND fusiondesk_display_color_conversion_tests)

add_executable(fusiondesk_feature_contract_tests
    tests/modules/feature/feature_contracts_smoke.cpp
)

target_link_libraries(fusiondesk_feature_contract_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_feature_contract_tests COMMAND fusiondesk_feature_contract_tests)

add_executable(fusiondesk_test_echo_module_tests
    tests/modules/test/test_echo_module_smoke.cpp
)

target_link_libraries(fusiondesk_test_echo_module_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_test_echo_module_tests COMMAND fusiondesk_test_echo_module_tests)

add_executable(fusiondesk_request_tracker_tests
    tests/core/network/request_tracker_smoke.cpp
)

target_link_libraries(fusiondesk_request_tracker_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_request_tracker_tests COMMAND fusiondesk_request_tracker_tests)

add_executable(fusiondesk_channel_allowlist_tests
    tests/core/network/channel_allowlist_smoke.cpp
)

target_link_libraries(fusiondesk_channel_allowlist_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_channel_allowlist_tests COMMAND fusiondesk_channel_allowlist_tests)

add_executable(fusiondesk_channel_registry_tests
    tests/core/network/channel_registry_smoke.cpp
)

target_link_libraries(fusiondesk_channel_registry_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_channel_registry_tests COMMAND fusiondesk_channel_registry_tests)

add_executable(fusiondesk_channel_defaults_tests
    tests/core/network/channel_defaults_smoke.cpp
)

target_link_libraries(fusiondesk_channel_defaults_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_channel_defaults_tests COMMAND fusiondesk_channel_defaults_tests)

add_executable(fusiondesk_priority_scheduler_tests
    tests/core/network/priority_scheduler_smoke.cpp
)

target_link_libraries(fusiondesk_priority_scheduler_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_priority_scheduler_tests COMMAND fusiondesk_priority_scheduler_tests)

add_executable(fusiondesk_network_manager_tests
    tests/core/network/network_manager_smoke.cpp
)

target_link_libraries(fusiondesk_network_manager_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_network_manager_tests COMMAND fusiondesk_network_manager_tests)

add_executable(fusiondesk_network_manager_transport_tests
    tests/core/network/network_manager_transport_smoke.cpp
)

target_link_libraries(fusiondesk_network_manager_transport_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_network_manager_transport_tests COMMAND fusiondesk_network_manager_transport_tests)

add_executable(fusiondesk_socket_group_tests
    tests/core/network/socket_group_smoke.cpp
)

target_link_libraries(fusiondesk_socket_group_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_socket_group_tests COMMAND fusiondesk_socket_group_tests)

add_executable(fusiondesk_session_manager_tests
    tests/core/session/session_manager_smoke.cpp
)

target_link_libraries(fusiondesk_session_manager_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_session_manager_tests COMMAND fusiondesk_session_manager_tests)

add_executable(fusiondesk_runtime_host_tests
    tests/runtime/runtime_host_smoke.cpp
)

target_link_libraries(fusiondesk_runtime_host_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_runtime_host_tests COMMAND fusiondesk_runtime_host_tests)

add_executable(fusiondesk_session_mainline_tests
    tests/runtime/session/session_mainline_smoke.cpp
)

target_link_libraries(fusiondesk_session_mainline_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_session_mainline_tests COMMAND fusiondesk_session_mainline_tests)

add_executable(fusiondesk_feature_runtime_service_tests
    tests/runtime/feature/feature_runtime_service_smoke.cpp
)

target_link_libraries(fusiondesk_feature_runtime_service_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_feature_runtime_service_tests COMMAND fusiondesk_feature_runtime_service_tests)

add_executable(fusiondesk_clipboard_runtime_service_tests
    tests/runtime/feature/clipboard_runtime_service_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_runtime_service_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_clipboard_runtime_service_tests COMMAND fusiondesk_clipboard_runtime_service_tests)

add_executable(fusiondesk_clipboard_runtime_change_monitor_tests
    tests/runtime/feature/clipboard_runtime_change_monitor_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_runtime_change_monitor_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_clipboard_runtime_change_monitor_tests
    COMMAND fusiondesk_clipboard_runtime_change_monitor_tests
)

add_executable(fusiondesk_clipboard_runtime_object_lock_tests
    tests/runtime/feature/clipboard_runtime_object_lock_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_runtime_object_lock_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_clipboard_runtime_object_lock_tests
    COMMAND fusiondesk_clipboard_runtime_object_lock_tests
)

add_executable(fusiondesk_clipboard_runtime_reconnect_tests
    tests/runtime/feature/clipboard_runtime_reconnect_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_runtime_reconnect_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_clipboard_runtime_reconnect_tests
    COMMAND fusiondesk_clipboard_runtime_reconnect_tests
)

add_executable(fusiondesk_clipboard_runtime_two_peer_reconnect_tests
    tests/runtime/feature/clipboard_runtime_two_peer_reconnect_smoke.cpp
)

target_link_libraries(fusiondesk_clipboard_runtime_two_peer_reconnect_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_clipboard_runtime_two_peer_reconnect_tests
    COMMAND fusiondesk_clipboard_runtime_two_peer_reconnect_tests
)

add_executable(fusiondesk_display_runtime_service_tests
    tests/runtime/display/display_runtime_service_smoke.cpp
)

target_link_libraries(fusiondesk_display_runtime_service_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_display_runtime_service_tests COMMAND fusiondesk_display_runtime_service_tests)

add_executable(fusiondesk_display_capture_backend_selection_tests
    tests/runtime/display/display_capture_backend_selection_smoke.cpp
)

target_link_libraries(fusiondesk_display_capture_backend_selection_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_capture_backend_selection_tests
    COMMAND fusiondesk_display_capture_backend_selection_tests
)

add_executable(fusiondesk_display_capture_backend_factory_tests
    tests/runtime/display/display_capture_backend_factory_smoke.cpp
)

target_link_libraries(fusiondesk_display_capture_backend_factory_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_capture_backend_factory_tests
    COMMAND fusiondesk_display_capture_backend_factory_tests
)

add_executable(fusiondesk_display_capture_backend_failover_tests
    tests/runtime/display/display_capture_backend_failover_smoke.cpp
)

target_link_libraries(fusiondesk_display_capture_backend_failover_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_capture_backend_failover_tests
    COMMAND fusiondesk_display_capture_backend_failover_tests
)

add_executable(fusiondesk_display_capture_options_tests
    tests/runtime/display/display_capture_options_smoke.cpp
)

target_link_libraries(fusiondesk_display_capture_options_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_capture_options_tests
    COMMAND fusiondesk_display_capture_options_tests
)

add_executable(fusiondesk_display_source_selection_tests
    tests/runtime/display/display_source_selection_smoke.cpp
)

target_link_libraries(fusiondesk_display_source_selection_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_source_selection_tests
    COMMAND fusiondesk_display_source_selection_tests
)

add_executable(fusiondesk_display_codec_selection_tests
    tests/runtime/display/display_codec_selection_smoke.cpp
)

target_link_libraries(fusiondesk_display_codec_selection_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_codec_selection_tests
    COMMAND fusiondesk_display_codec_selection_tests
)

add_executable(fusiondesk_display_codec_negotiation_tests
    tests/runtime/display/display_codec_negotiation_smoke.cpp
)

target_link_libraries(fusiondesk_display_codec_negotiation_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_codec_negotiation_tests
    COMMAND fusiondesk_display_codec_negotiation_tests
)

add_executable(fusiondesk_display_codec_peer_profile_tests
    tests/runtime/display/display_codec_peer_profile_smoke.cpp
)

target_link_libraries(fusiondesk_display_codec_peer_profile_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_codec_peer_profile_tests
    COMMAND fusiondesk_display_codec_peer_profile_tests
)

add_executable(fusiondesk_display_codec_backend_factory_tests
    tests/runtime/display/display_codec_backend_factory_smoke.cpp
)

target_link_libraries(fusiondesk_display_codec_backend_factory_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_codec_backend_factory_tests
    COMMAND fusiondesk_display_codec_backend_factory_tests
)

add_executable(fusiondesk_display_capture_platform_plan_tests
    tests/runtime/display/display_capture_platform_plan_smoke.cpp
)

target_link_libraries(fusiondesk_display_capture_platform_plan_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_capture_platform_plan_tests
    COMMAND fusiondesk_display_capture_platform_plan_tests
)

add_executable(fusiondesk_display_capture_recovery_tests
    tests/runtime/display/display_capture_recovery_smoke.cpp
)

target_link_libraries(fusiondesk_display_capture_recovery_tests
    PRIVATE
        fusiondesk_core
)

add_test(
    NAME fusiondesk_display_capture_recovery_tests
    COMMAND fusiondesk_display_capture_recovery_tests
)

add_executable(fusiondesk_peer_connection_plan_tests
    tests/runtime/connection/peer_connection_plan_smoke.cpp
)

target_link_libraries(fusiondesk_peer_connection_plan_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_peer_connection_plan_tests COMMAND fusiondesk_peer_connection_plan_tests)

add_executable(fusiondesk_peer_profile_exchange_tests
    tests/runtime/connection/peer_profile_exchange_smoke.cpp
)

target_link_libraries(fusiondesk_peer_profile_exchange_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_peer_profile_exchange_tests COMMAND fusiondesk_peer_profile_exchange_tests)

add_executable(fusiondesk_peer_profile_service_tests
    tests/runtime/connection/peer_profile_service_smoke.cpp
)

target_link_libraries(fusiondesk_peer_profile_service_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_peer_profile_service_tests COMMAND fusiondesk_peer_profile_service_tests)

add_executable(fusiondesk_peer_profile_runtime_service_tests
    tests/runtime/connection/peer_profile_runtime_service_smoke.cpp
)

target_link_libraries(fusiondesk_peer_profile_runtime_service_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_peer_profile_runtime_service_tests COMMAND fusiondesk_peer_profile_runtime_service_tests)

add_executable(fusiondesk_module_inventory_exchange_tests
    tests/runtime/connection/module_inventory_exchange_smoke.cpp
)

target_link_libraries(fusiondesk_module_inventory_exchange_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_module_inventory_exchange_tests COMMAND fusiondesk_module_inventory_exchange_tests)

add_executable(fusiondesk_peer_coordination_tests
    tests/runtime/connection/peer_coordination_smoke.cpp
)

target_link_libraries(fusiondesk_peer_coordination_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_peer_coordination_tests COMMAND fusiondesk_peer_coordination_tests)

add_executable(fusiondesk_reconnect_orchestration_plan_tests
    tests/runtime/connection/reconnect_orchestration_plan_smoke.cpp
)

target_link_libraries(fusiondesk_reconnect_orchestration_plan_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_reconnect_orchestration_plan_tests COMMAND fusiondesk_reconnect_orchestration_plan_tests)

add_executable(fusiondesk_reconnect_coordinator_tests
    tests/runtime/connection/reconnect_coordinator_smoke.cpp
)

target_link_libraries(fusiondesk_reconnect_coordinator_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_reconnect_coordinator_tests COMMAND fusiondesk_reconnect_coordinator_tests)

add_executable(fusiondesk_reconnect_diagnostics_report_tests
    tests/runtime/connection/reconnect_diagnostics_report_smoke.cpp
)

target_link_libraries(fusiondesk_reconnect_diagnostics_report_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_reconnect_diagnostics_report_tests COMMAND fusiondesk_reconnect_diagnostics_report_tests)

add_executable(fusiondesk_reconnect_teardown_ack_tests
    tests/runtime/connection/reconnect_teardown_ack_smoke.cpp
)

target_link_libraries(fusiondesk_reconnect_teardown_ack_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_reconnect_teardown_ack_tests COMMAND fusiondesk_reconnect_teardown_ack_tests)

add_executable(fusiondesk_reconnect_teardown_dispatch_tests
    tests/runtime/connection/reconnect_teardown_dispatch_smoke.cpp
)

target_link_libraries(fusiondesk_reconnect_teardown_dispatch_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_reconnect_teardown_dispatch_tests COMMAND fusiondesk_reconnect_teardown_dispatch_tests)

add_executable(fusiondesk_reconnect_teardown_handler_tests
    tests/runtime/connection/reconnect_teardown_handler_smoke.cpp
)

target_link_libraries(fusiondesk_reconnect_teardown_handler_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_reconnect_teardown_handler_tests COMMAND fusiondesk_reconnect_teardown_handler_tests)

add_executable(fusiondesk_reconnect_teardown_service_tests
    tests/runtime/connection/reconnect_teardown_service_smoke.cpp
)

target_link_libraries(fusiondesk_reconnect_teardown_service_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_reconnect_teardown_service_tests COMMAND fusiondesk_reconnect_teardown_service_tests)

add_executable(fusiondesk_tunnel_reconnect_executor_tests
    tests/runtime/tunnel/tunnel_reconnect_executor_smoke.cpp
)

target_link_libraries(fusiondesk_tunnel_reconnect_executor_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_tunnel_reconnect_executor_tests COMMAND fusiondesk_tunnel_reconnect_executor_tests)

add_executable(fusiondesk_protocol_validator_tests
    tests/core/protocol/protocol_validator_smoke.cpp
)

target_link_libraries(fusiondesk_protocol_validator_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_protocol_validator_tests COMMAND fusiondesk_protocol_validator_tests)

add_executable(fusiondesk_packet_codec_tests
    tests/core/protocol/packet_codec_smoke.cpp
)

target_link_libraries(fusiondesk_packet_codec_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_packet_codec_tests COMMAND fusiondesk_packet_codec_tests)

add_executable(fusiondesk_capability_exchange_tests
    tests/core/protocol/capability_exchange_smoke.cpp
)

target_link_libraries(fusiondesk_capability_exchange_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_capability_exchange_tests COMMAND fusiondesk_capability_exchange_tests)

add_executable(fusiondesk_capability_negotiation_tests
    tests/core/protocol/capability_negotiation_smoke.cpp
)

target_link_libraries(fusiondesk_capability_negotiation_tests
    PRIVATE
        fusiondesk_core
)

add_test(NAME fusiondesk_capability_negotiation_tests COMMAND fusiondesk_capability_negotiation_tests)

set(FUSIONDESK_TEST_TARGETS
    fusiondesk_module_host_tests
    fusiondesk_module_catalog_tests
    fusiondesk_module_ingress_tests
    fusiondesk_module_factory_tests
    fusiondesk_module_protocol_tests
    fusiondesk_clipboard_fdcl_codec_tests
    fusiondesk_clipboard_transfer_tests
    fusiondesk_clipboard_large_data_scheduler_tests
    fusiondesk_clipboard_module_large_data_tests
    fusiondesk_clipboard_module_file_transfer_tests
    fusiondesk_clipboard_module_drag_tests
    fusiondesk_clipboard_drag_coordinates_tests
    fusiondesk_clipboard_module_tests
    fusiondesk_clipboard_module_object_lock_tests
    fusiondesk_display_mvp_tests
    fusiondesk_display_frame_codec_tests
    fusiondesk_display_encoded_video_payload_tests
    fusiondesk_display_color_conversion_tests
    fusiondesk_feature_contract_tests
    fusiondesk_test_echo_module_tests
    fusiondesk_request_tracker_tests
    fusiondesk_channel_allowlist_tests
    fusiondesk_channel_registry_tests
    fusiondesk_channel_defaults_tests
    fusiondesk_priority_scheduler_tests
    fusiondesk_network_manager_tests
    fusiondesk_network_manager_transport_tests
    fusiondesk_socket_group_tests
    fusiondesk_session_manager_tests
    fusiondesk_runtime_host_tests
    fusiondesk_session_mainline_tests
    fusiondesk_feature_runtime_service_tests
    fusiondesk_clipboard_runtime_service_tests
    fusiondesk_clipboard_runtime_change_monitor_tests
    fusiondesk_clipboard_runtime_object_lock_tests
    fusiondesk_clipboard_runtime_reconnect_tests
    fusiondesk_clipboard_runtime_two_peer_reconnect_tests
    fusiondesk_display_runtime_service_tests
    fusiondesk_display_capture_backend_selection_tests
    fusiondesk_display_capture_backend_factory_tests
    fusiondesk_display_capture_backend_failover_tests
    fusiondesk_display_capture_options_tests
    fusiondesk_display_source_selection_tests
    fusiondesk_display_codec_selection_tests
    fusiondesk_display_codec_negotiation_tests
    fusiondesk_display_codec_peer_profile_tests
    fusiondesk_display_codec_backend_factory_tests
    fusiondesk_display_capture_platform_plan_tests
    fusiondesk_display_capture_recovery_tests
    fusiondesk_peer_connection_plan_tests
    fusiondesk_peer_profile_exchange_tests
    fusiondesk_peer_profile_service_tests
    fusiondesk_peer_profile_runtime_service_tests
    fusiondesk_module_inventory_exchange_tests
    fusiondesk_peer_coordination_tests
    fusiondesk_reconnect_orchestration_plan_tests
    fusiondesk_reconnect_coordinator_tests
    fusiondesk_reconnect_diagnostics_report_tests
    fusiondesk_reconnect_teardown_ack_tests
    fusiondesk_reconnect_teardown_dispatch_tests
    fusiondesk_reconnect_teardown_handler_tests
    fusiondesk_reconnect_teardown_service_tests
    fusiondesk_tunnel_reconnect_executor_tests
    fusiondesk_protocol_validator_tests
    fusiondesk_packet_codec_tests
    fusiondesk_capability_exchange_tests
    fusiondesk_capability_negotiation_tests
)

if(WIN32)
    add_library(fusiondesk_platform_windows_display
        src/platform/windows/display/windows_cursor_overlay.cpp
        src/platform/windows/display/windows_display_capture_factory.cpp
        src/platform/windows/display/windows_dxgi_desktop_duplication_capture.cpp
        src/platform/windows/display/windows_gdi_display_capture.cpp
        src/platform/windows/display/windows_graphics_capture.cpp
        src/platform/windows/display/windows_media_foundation_display_codec.cpp
        src/platform/windows/display/windows_production_display_capture_factories.cpp
    )

    target_include_directories(fusiondesk_platform_windows_display
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_features(fusiondesk_platform_windows_display PUBLIC cxx_std_17)

    target_link_libraries(fusiondesk_platform_windows_display
        PUBLIC
            fusiondesk_core
        PRIVATE
            d3d11
            dxgi
            runtimeobject
            mfplat
            mfuuid
            ole32
            user32
            gdi32
    )

    add_executable(fusiondesk_windows_gdi_display_capture_tests
        tests/platform/windows/display/windows_gdi_display_capture_smoke.cpp
    )

    target_link_libraries(fusiondesk_windows_gdi_display_capture_tests
        PRIVATE
            fusiondesk_platform_windows_display
    )

    add_test(NAME fusiondesk_windows_gdi_display_capture_tests COMMAND fusiondesk_windows_gdi_display_capture_tests)
    list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_windows_gdi_display_capture_tests)

    add_executable(fusiondesk_windows_dxgi_display_capture_opt_in_tests
        tests/platform/windows/display/windows_dxgi_display_capture_opt_in_smoke.cpp
    )

    target_link_libraries(fusiondesk_windows_dxgi_display_capture_opt_in_tests
        PRIVATE
            fusiondesk_platform_windows_display
    )

    add_test(
        NAME fusiondesk_windows_dxgi_display_capture_opt_in_tests
        COMMAND fusiondesk_windows_dxgi_display_capture_opt_in_tests
    )
    list(APPEND FUSIONDESK_TEST_TARGETS
        fusiondesk_windows_dxgi_display_capture_opt_in_tests)

    add_executable(fusiondesk_windows_wgc_display_capture_opt_in_tests
        tests/platform/windows/display/windows_wgc_display_capture_opt_in_smoke.cpp
    )

    target_link_libraries(fusiondesk_windows_wgc_display_capture_opt_in_tests
        PRIVATE
            fusiondesk_platform_windows_display
    )

    add_test(
        NAME fusiondesk_windows_wgc_display_capture_opt_in_tests
        COMMAND fusiondesk_windows_wgc_display_capture_opt_in_tests
    )
    list(APPEND FUSIONDESK_TEST_TARGETS
        fusiondesk_windows_wgc_display_capture_opt_in_tests)

    add_executable(fusiondesk_windows_media_foundation_display_codec_tests
        tests/platform/windows/display/windows_media_foundation_display_codec_smoke.cpp
    )

    target_link_libraries(fusiondesk_windows_media_foundation_display_codec_tests
        PRIVATE
            fusiondesk_platform_windows_display
    )

    add_test(
        NAME fusiondesk_windows_media_foundation_display_codec_tests
        COMMAND fusiondesk_windows_media_foundation_display_codec_tests
    )
    list(APPEND FUSIONDESK_TEST_TARGETS
        fusiondesk_windows_media_foundation_display_codec_tests)

    add_library(fusiondesk_platform_windows_feature_adapters
        src/platform/windows/clipboard/windows_clipboard_format_conversion.cpp
        src/platform/windows/clipboard/windows_clipboard_local_files.cpp
        src/platform/windows/clipboard/windows_clipboard_ole_data_object.cpp
        src/platform/windows/clipboard/windows_clipboard_drag.cpp
        src/platform/windows/clipboard/windows_clipboard_image_transcoding.cpp
        src/platform/windows/clipboard/windows_clipboard_native_image.cpp
        src/platform/windows/clipboard/windows_clipboard_rendering.cpp
        src/platform/windows/clipboard/windows_clipboard_native_publish.cpp
        src/platform/windows/clipboard/windows_clipboard_native_snapshot.cpp
        src/platform/windows/clipboard/windows_clipboard_snapshot.cpp
        src/platform/windows/clipboard/windows_clipboard_endpoint.cpp
        src/platform/windows/input/windows_input_injector.cpp
    )

    target_include_directories(fusiondesk_platform_windows_feature_adapters
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_features(fusiondesk_platform_windows_feature_adapters PUBLIC cxx_std_17)

    target_link_libraries(fusiondesk_platform_windows_feature_adapters
        PUBLIC
            fusiondesk_core
        PRIVATE
            ole32
            shell32
            user32
            windowscodecs
            uuid
    )

    add_executable(fusiondesk_windows_clipboard_endpoint_tests
        tests/platform/windows/feature/windows_clipboard_endpoint_smoke.cpp
    )

    target_link_libraries(fusiondesk_windows_clipboard_endpoint_tests
        PRIVATE
            fusiondesk_platform_windows_feature_adapters
    )
    target_include_directories(fusiondesk_windows_clipboard_endpoint_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/clipboard
    )

    add_test(
        NAME fusiondesk_windows_clipboard_endpoint_tests
        COMMAND fusiondesk_windows_clipboard_endpoint_tests
    )
    list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_windows_clipboard_endpoint_tests)

    add_executable(fusiondesk_windows_clipboard_image_endpoint_tests
        tests/platform/windows/feature/windows_clipboard_image_endpoint_smoke.cpp
    )

    target_link_libraries(fusiondesk_windows_clipboard_image_endpoint_tests
        PRIVATE
            fusiondesk_platform_windows_feature_adapters
    )
    target_include_directories(fusiondesk_windows_clipboard_image_endpoint_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/clipboard
    )

    add_test(
        NAME fusiondesk_windows_clipboard_image_endpoint_tests
        COMMAND fusiondesk_windows_clipboard_image_endpoint_tests
    )
    list(APPEND FUSIONDESK_TEST_TARGETS
        fusiondesk_windows_clipboard_image_endpoint_tests)

    add_executable(fusiondesk_windows_clipboard_watcher_tests
        tests/platform/windows/feature/windows_clipboard_watcher_smoke.cpp
    )

    target_link_libraries(fusiondesk_windows_clipboard_watcher_tests
        PRIVATE
            fusiondesk_platform_windows_feature_adapters
    )

    add_test(
        NAME fusiondesk_windows_clipboard_watcher_tests
        COMMAND fusiondesk_windows_clipboard_watcher_tests
    )
    list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_windows_clipboard_watcher_tests)

    add_executable(fusiondesk_windows_clipboard_local_files_tests
        tests/platform/windows/feature/windows_clipboard_local_files_smoke.cpp
    )

    target_include_directories(fusiondesk_windows_clipboard_local_files_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/clipboard
    )

    target_link_libraries(fusiondesk_windows_clipboard_local_files_tests
        PRIVATE
            fusiondesk_platform_windows_feature_adapters
    )

    add_test(
        NAME fusiondesk_windows_clipboard_local_files_tests
        COMMAND fusiondesk_windows_clipboard_local_files_tests
    )
    list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_windows_clipboard_local_files_tests)

    add_executable(fusiondesk_windows_clipboard_remote_file_stream_tests
        tests/platform/windows/feature/windows_clipboard_remote_file_stream_smoke.cpp
    )

    target_include_directories(fusiondesk_windows_clipboard_remote_file_stream_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/clipboard
    )

    target_link_libraries(fusiondesk_windows_clipboard_remote_file_stream_tests
        PRIVATE
            fusiondesk_platform_windows_feature_adapters
    )

    add_test(
        NAME fusiondesk_windows_clipboard_remote_file_stream_tests
        COMMAND fusiondesk_windows_clipboard_remote_file_stream_tests
    )
    list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_windows_clipboard_remote_file_stream_tests)

    add_executable(fusiondesk_windows_clipboard_drag_coordinate_mapper_tests
        tests/platform/windows/feature/windows_clipboard_drag_coordinate_mapper_smoke.cpp
    )

    target_link_libraries(fusiondesk_windows_clipboard_drag_coordinate_mapper_tests
        PRIVATE
            fusiondesk_platform_windows_feature_adapters
    )

    add_test(
        NAME fusiondesk_windows_clipboard_drag_coordinate_mapper_tests
        COMMAND fusiondesk_windows_clipboard_drag_coordinate_mapper_tests
    )
    list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_windows_clipboard_drag_coordinate_mapper_tests)

    add_executable(fusiondesk_windows_feature_adapter_tests
        tests/platform/windows/feature/windows_input_adapters_smoke.cpp
    )

    target_link_libraries(fusiondesk_windows_feature_adapter_tests
        PRIVATE
            fusiondesk_platform_windows_feature_adapters
    )

    add_test(NAME fusiondesk_windows_feature_adapter_tests COMMAND fusiondesk_windows_feature_adapter_tests)
    list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_windows_feature_adapter_tests)
endif()

if(APPLE)
    enable_language(OBJCXX)

    add_library(fusiondesk_platform_macos_feature_adapters
        src/platform/macos/clipboard/mac_clipboard_endpoint.mm
        src/platform/macos/clipboard/mac_clipboard_local_files.cpp
    )

    target_include_directories(fusiondesk_platform_macos_feature_adapters
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_features(fusiondesk_platform_macos_feature_adapters PUBLIC cxx_std_17)
    target_compile_options(fusiondesk_platform_macos_feature_adapters
        PRIVATE
            -fno-objc-arc
    )

    target_link_libraries(fusiondesk_platform_macos_feature_adapters
        PUBLIC
            fusiondesk_core
        PRIVATE
            "-framework AppKit"
            "-framework Foundation"
    )

    add_executable(fusiondesk_macos_clipboard_endpoint_tests
        tests/platform/macos/feature/mac_clipboard_endpoint_smoke.cpp
    )

    target_link_libraries(fusiondesk_macos_clipboard_endpoint_tests
        PRIVATE
            fusiondesk_platform_macos_feature_adapters
    )

    add_test(
        NAME fusiondesk_macos_clipboard_endpoint_tests
        COMMAND fusiondesk_macos_clipboard_endpoint_tests
    )
    list(APPEND FUSIONDESK_TEST_TARGETS
        fusiondesk_macos_clipboard_endpoint_tests)
endif()

option(FUSIONDESK_BUILD_QT_ADAPTERS "Build optional Qt adapters when Qt is available" ON)

if(FUSIONDESK_BUILD_QT_ADAPTERS)
    find_program(FUSIONDESK_QMAKE_EXECUTABLE NAMES qmake6 qmake)
    if(FUSIONDESK_QMAKE_EXECUTABLE)
        execute_process(
            COMMAND ${FUSIONDESK_QMAKE_EXECUTABLE} -query QT_INSTALL_PREFIX
            OUTPUT_VARIABLE FUSIONDESK_QT_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(FUSIONDESK_QT_PREFIX)
            list(APPEND CMAKE_PREFIX_PATH "${FUSIONDESK_QT_PREFIX}")
        endif()
    endif()

    find_package(Qt6 QUIET COMPONENTS Core Network)
    if(Qt6_FOUND)
        set(FUSIONDESK_QT_LIBS Qt6::Core Qt6::Network)
        find_package(Qt6 QUIET COMPONENTS Gui)
        if(TARGET Qt6::Gui)
            set(FUSIONDESK_QT_GUI_LIB Qt6::Gui)
        endif()
        find_package(Qt6 QUIET COMPONENTS Widgets)
        if(TARGET Qt6::Widgets)
            set(FUSIONDESK_QT_WIDGETS_LIB Qt6::Widgets)
        endif()
    else()
        find_package(Qt5 QUIET COMPONENTS Core Network)
        if(Qt5_FOUND)
            set(FUSIONDESK_QT_LIBS Qt5::Core Qt5::Network)
            find_package(Qt5 QUIET COMPONENTS Gui)
            if(TARGET Qt5::Gui)
                set(FUSIONDESK_QT_GUI_LIB Qt5::Gui)
            endif()
            find_package(Qt5 QUIET COMPONENTS Widgets)
            if(TARGET Qt5::Widgets)
                set(FUSIONDESK_QT_WIDGETS_LIB Qt5::Widgets)
            endif()
        endif()
    endif()

    if(FUSIONDESK_QT_LIBS)
        add_library(fusiondesk_qt_adapters
            src/adapters/qt/qt_channel_binder.cpp
            src/adapters/qt/qt_packet_channel.cpp
            src/adapters/qt/qt_tcp_transport_socket.cpp
        )

        target_include_directories(fusiondesk_qt_adapters
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/include
        )

        target_compile_features(fusiondesk_qt_adapters PUBLIC cxx_std_17)

        target_link_libraries(fusiondesk_qt_adapters
            PUBLIC
                fusiondesk_core
            PRIVATE
                ${FUSIONDESK_QT_LIBS}
        )

        add_library(fusiondesk_qt_runtime
            src/runtime/qt/qt_event_loop_bridge.cpp
            src/runtime/qt/qt_peer_profile_coordinator.cpp
            src/runtime/qt/qt_peer_profile_runtime_service.cpp
            src/runtime/qt/qt_reconnect_executor.cpp
            src/runtime/qt/qt_reconnect_orchestrator.cpp
            src/runtime/qt/qt_reconnect_runtime_service.cpp
            src/runtime/qt/qt_timer_bridge.cpp
            src/runtime/qt/qt_transport_profile.cpp
        )

        target_include_directories(fusiondesk_qt_runtime
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/include
        )

        target_compile_features(fusiondesk_qt_runtime PUBLIC cxx_std_17)

        target_link_libraries(fusiondesk_qt_runtime
            PUBLIC
                fusiondesk_core
                fusiondesk_qt_adapters
            PRIVATE
                ${FUSIONDESK_QT_LIBS}
        )

        add_executable(fusiondesk_qt_tcp_transport_tests
            tests/adapters/qt/qt_tcp_transport_smoke.cpp
        )

        target_link_libraries(fusiondesk_qt_tcp_transport_tests
            PRIVATE
                fusiondesk_qt_runtime
                ${FUSIONDESK_QT_LIBS}
        )

        add_test(NAME fusiondesk_qt_tcp_transport_tests COMMAND fusiondesk_qt_tcp_transport_tests)
        list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_qt_tcp_transport_tests)

        add_executable(fusiondesk_qt_timer_bridge_tests
            tests/runtime/qt/qt_timer_bridge_smoke.cpp
        )

        target_link_libraries(fusiondesk_qt_timer_bridge_tests
            PRIVATE
                fusiondesk_qt_runtime
                ${FUSIONDESK_QT_LIBS}
        )

        add_test(NAME fusiondesk_qt_timer_bridge_tests COMMAND fusiondesk_qt_timer_bridge_tests)
        list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_qt_timer_bridge_tests)

        add_executable(fusiondesk_qt_event_loop_bridge_tests
            tests/runtime/qt/qt_event_loop_bridge_smoke.cpp
        )

        target_link_libraries(fusiondesk_qt_event_loop_bridge_tests
            PRIVATE
                fusiondesk_qt_runtime
                ${FUSIONDESK_QT_LIBS}
        )

        add_test(NAME fusiondesk_qt_event_loop_bridge_tests COMMAND fusiondesk_qt_event_loop_bridge_tests)
        list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_qt_event_loop_bridge_tests)

        add_executable(fusiondesk_qt_peer_profile_coordinator_tests
            tests/runtime/qt/qt_peer_profile_coordinator_smoke.cpp
        )

        target_link_libraries(fusiondesk_qt_peer_profile_coordinator_tests
            PRIVATE
                fusiondesk_qt_runtime
                ${FUSIONDESK_QT_LIBS}
        )

        add_test(NAME fusiondesk_qt_peer_profile_coordinator_tests COMMAND fusiondesk_qt_peer_profile_coordinator_tests)
        list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_qt_peer_profile_coordinator_tests)

        add_executable(fusiondesk_qt_peer_profile_runtime_service_tests
            tests/runtime/qt/qt_peer_profile_runtime_service_smoke.cpp
        )

        target_link_libraries(fusiondesk_qt_peer_profile_runtime_service_tests
            PRIVATE
                fusiondesk_qt_runtime
                ${FUSIONDESK_QT_LIBS}
        )

        add_test(NAME fusiondesk_qt_peer_profile_runtime_service_tests COMMAND fusiondesk_qt_peer_profile_runtime_service_tests)
        list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_qt_peer_profile_runtime_service_tests)

        if(FUSIONDESK_QT_GUI_LIB)
            add_library(fusiondesk_qt_display_adapters
                src/adapters/qt/display/qt_image_display_renderer.cpp
            )

            target_include_directories(fusiondesk_qt_display_adapters
                PUBLIC
                    ${CMAKE_CURRENT_SOURCE_DIR}/include
            )

            target_compile_features(fusiondesk_qt_display_adapters PUBLIC cxx_std_17)

            target_link_libraries(fusiondesk_qt_display_adapters
                PUBLIC
                    fusiondesk_core
                PRIVATE
                    ${FUSIONDESK_QT_GUI_LIB}
            )

            add_executable(fusiondesk_qt_image_display_renderer_tests
                tests/adapters/qt/display/qt_image_display_renderer_smoke.cpp
            )

            target_link_libraries(fusiondesk_qt_image_display_renderer_tests
                PRIVATE
                    fusiondesk_qt_display_adapters
                    ${FUSIONDESK_QT_GUI_LIB}
            )

            add_test(NAME fusiondesk_qt_image_display_renderer_tests COMMAND fusiondesk_qt_image_display_renderer_tests)
            list(APPEND FUSIONDESK_TEST_TARGETS fusiondesk_qt_image_display_renderer_tests)

            if(FUSIONDESK_QT_WIDGETS_LIB)
                add_library(fusiondesk_qt_widget_display_adapters
                    src/adapters/qt/display/qt_image_display_window.cpp
                )

                target_include_directories(fusiondesk_qt_widget_display_adapters
                    PUBLIC
                        ${CMAKE_CURRENT_SOURCE_DIR}/include
                )

                target_compile_features(fusiondesk_qt_widget_display_adapters PUBLIC cxx_std_17)

                target_link_libraries(fusiondesk_qt_widget_display_adapters
                    PUBLIC
                        fusiondesk_qt_display_adapters
                        ${FUSIONDESK_QT_WIDGETS_LIB}
                )

                add_executable(fusiondesk_qt_image_display_window_tests
                    tests/adapters/qt/display/qt_image_display_window_smoke.cpp
                )

                target_link_libraries(fusiondesk_qt_image_display_window_tests
                    PRIVATE
                        fusiondesk_qt_widget_display_adapters
                        ${FUSIONDESK_QT_WIDGETS_LIB}
                )

                add_test(
                    NAME fusiondesk_qt_image_display_window_tests
                    COMMAND fusiondesk_qt_image_display_window_tests
                )
                list(APPEND FUSIONDESK_TEST_TARGETS
                    fusiondesk_qt_image_display_window_tests)
            endif()

            add_library(fusiondesk_qt_feature_adapters
                src/adapters/qt/clipboard/qt_clipboard_endpoint.cpp
                src/adapters/qt/clipboard/qt_clipboard_image_formats.cpp
                src/adapters/qt/clipboard/qt_clipboard_local_files.cpp
                src/adapters/qt/clipboard/qt_clipboard_text_formats.cpp
                src/adapters/qt/input/qt_input_capture.cpp
            )

            target_include_directories(fusiondesk_qt_feature_adapters
                PUBLIC
                    ${CMAKE_CURRENT_SOURCE_DIR}/include
            )

            target_compile_features(fusiondesk_qt_feature_adapters PUBLIC cxx_std_17)

            target_link_libraries(fusiondesk_qt_feature_adapters
                PUBLIC
                    fusiondesk_core
                PRIVATE
                    ${FUSIONDESK_QT_GUI_LIB}
            )

            add_executable(fusiondesk_qt_input_capture_tests
                tests/adapters/qt/input/qt_input_capture_smoke.cpp
            )

            target_link_libraries(fusiondesk_qt_input_capture_tests
                PRIVATE
                    fusiondesk_qt_feature_adapters
                    ${FUSIONDESK_QT_GUI_LIB}
            )

            add_test(NAME fusiondesk_qt_input_capture_tests COMMAND fusiondesk_qt_input_capture_tests)
            list(APPEND FUSIONDESK_TEST_TARGETS
                fusiondesk_qt_input_capture_tests
            )

            add_executable(fusiondesk_qt_clipboard_endpoint_tests
                tests/adapters/qt/clipboard/qt_clipboard_endpoint_smoke.cpp
            )

            target_link_libraries(fusiondesk_qt_clipboard_endpoint_tests
                PRIVATE
                    fusiondesk_qt_feature_adapters
                    ${FUSIONDESK_QT_GUI_LIB}
            )

            add_test(
                NAME fusiondesk_qt_clipboard_endpoint_tests
                COMMAND fusiondesk_qt_clipboard_endpoint_tests
            )
            list(APPEND FUSIONDESK_TEST_TARGETS
                fusiondesk_qt_clipboard_endpoint_tests
            )

            add_executable(fusiondesk_qt_clipboard_image_endpoint_tests
                tests/adapters/qt/clipboard/qt_clipboard_image_endpoint_smoke.cpp
            )

            target_link_libraries(fusiondesk_qt_clipboard_image_endpoint_tests
                PRIVATE
                    fusiondesk_qt_feature_adapters
                    ${FUSIONDESK_QT_GUI_LIB}
            )

            add_test(
                NAME fusiondesk_qt_clipboard_image_endpoint_tests
                COMMAND fusiondesk_qt_clipboard_image_endpoint_tests
            )
            list(APPEND FUSIONDESK_TEST_TARGETS
                fusiondesk_qt_clipboard_image_endpoint_tests
            )
        endif()

        add_library(fusiondesk_pc_shell_common
            apps/pc/common/pc_app_shell.cpp
            apps/pc/common/pc_clipboard_drag_mapper.cpp
            apps/pc/common/pc_clipboard_file_save.cpp
            apps/pc/common/pc_clipboard_formatted_text_support.cpp
            apps/pc/common/pc_clipboard_image_support.cpp
            apps/pc/common/pc_clipboard_policy_file.cpp
            apps/pc/common/pc_clipboard_shell.cpp
            apps/pc/common/pc_display_diagnostics.cpp
            apps/pc/common/pc_profile_dependencies.cpp
            apps/pc/common/pc_profile_options.cpp
            apps/pc/common/pc_product_session_controller.cpp
            apps/pc/common/pc_product_session_controller_display_codec.cpp
            apps/pc/common/pc_runtime_exchange_shell.cpp
            apps/pc/common/pc_session_diagnostics.cpp
            apps/pc/common/pc_shell_options.cpp
        )

        target_include_directories(fusiondesk_pc_shell_common
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/apps/pc/common
        )

        target_compile_features(fusiondesk_pc_shell_common PUBLIC cxx_std_17)

        target_link_libraries(fusiondesk_pc_shell_common
            PUBLIC
                fusiondesk_qt_runtime
            PRIVATE
                ${FUSIONDESK_QT_LIBS}
        )

        if(TARGET fusiondesk_platform_windows_display)
            target_link_libraries(fusiondesk_pc_shell_common
                PRIVATE
                    fusiondesk_platform_windows_display
            )
            target_compile_definitions(fusiondesk_pc_shell_common
                PRIVATE
                    FUSIONDESK_PC_HAS_WINDOWS_GDI_DISPLAY=1
                    FUSIONDESK_PC_HAS_WINDOWS_DISPLAY_CAPTURE=1
            )
        endif()

        if(TARGET fusiondesk_qt_display_adapters)
            target_link_libraries(fusiondesk_pc_shell_common
                PRIVATE
                    fusiondesk_qt_display_adapters
            )
            target_compile_definitions(fusiondesk_pc_shell_common
                PRIVATE
                    FUSIONDESK_PC_HAS_QT_IMAGE_DISPLAY=1
            )
        endif()

        if(TARGET fusiondesk_qt_widget_display_adapters)
            target_link_libraries(fusiondesk_pc_shell_common
                PRIVATE
                    fusiondesk_qt_widget_display_adapters
            )
            target_compile_definitions(fusiondesk_pc_shell_common
                PRIVATE
                    FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY=1
            )
        endif()

        if(TARGET fusiondesk_platform_windows_feature_adapters)
            target_link_libraries(fusiondesk_pc_shell_common
                PRIVATE
                    fusiondesk_platform_windows_feature_adapters
            )
            target_compile_definitions(fusiondesk_pc_shell_common
                PRIVATE
                    FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS=1
            )
        endif()

        if(TARGET fusiondesk_platform_macos_feature_adapters)
            target_link_libraries(fusiondesk_pc_shell_common
                PRIVATE
                    fusiondesk_platform_macos_feature_adapters
            )
            target_compile_definitions(fusiondesk_pc_shell_common
                PRIVATE
                    FUSIONDESK_PC_HAS_MACOS_FEATURE_ADAPTERS=1
            )
        endif()

        if(TARGET fusiondesk_qt_feature_adapters)
            target_link_libraries(fusiondesk_pc_shell_common
                PRIVATE
                    fusiondesk_qt_feature_adapters
            )
            target_compile_definitions(fusiondesk_pc_shell_common
                PRIVATE
                    FUSIONDESK_PC_HAS_QT_FEATURE_ADAPTERS=1
            )
        endif()

        add_executable(fusiondesk_pc_client
            apps/pc/client/main.cpp
        )

        target_link_libraries(fusiondesk_pc_client
            PRIVATE
                fusiondesk_pc_shell_common
        )

        add_executable(fusiondesk_pc_agent
            apps/pc/agent/main.cpp
        )

        target_link_libraries(fusiondesk_pc_agent
            PRIVATE
                fusiondesk_pc_shell_common
        )

        add_executable(fusiondesk_pc_profile_plan
            apps/pc/profile_plan/main.cpp
        )

        target_link_libraries(fusiondesk_pc_profile_plan
            PRIVATE
                fusiondesk_qt_runtime
                ${FUSIONDESK_QT_LIBS}
        )

        add_executable(fusiondesk_pc_agent_listen_profile_smoke
            tests/apps/pc/pc_agent_listen_profile_smoke.cpp
        )

        target_link_libraries(fusiondesk_pc_agent_listen_profile_smoke
            PRIVATE
                fusiondesk_pc_shell_common
        )

        add_executable(fusiondesk_pc_profile_plan_smoke
            tests/apps/pc/pc_profile_plan_smoke.cpp
        )

        add_executable(fusiondesk_pc_feature_adapter_startup_smoke
            tests/apps/pc/pc_feature_adapter_startup_smoke.cpp
        )

        add_executable(fusiondesk_pc_product_session_controller_tests
            tests/apps/pc/pc_product_session_controller_smoke.cpp
        )

        target_compile_definitions(fusiondesk_pc_profile_plan_smoke
            PRIVATE
                FUSIONDESK_PC_PROFILE_PLAN_EXE="$<TARGET_FILE:fusiondesk_pc_profile_plan>"
        )

        target_link_libraries(fusiondesk_pc_profile_plan_smoke
            PRIVATE
                fusiondesk_qt_runtime
                ${FUSIONDESK_QT_LIBS}
        )

        target_link_libraries(fusiondesk_pc_feature_adapter_startup_smoke
            PRIVATE
                fusiondesk_pc_shell_common
        )

        target_link_libraries(fusiondesk_pc_product_session_controller_tests
            PRIVATE
                fusiondesk_pc_shell_common
                ${FUSIONDESK_QT_LIBS}
        )

        if(TARGET fusiondesk_platform_windows_feature_adapters)
            add_executable(fusiondesk_pc_two_peer_clipboard_text_smoke
                tests/apps/pc/pc_two_peer_clipboard_text_smoke.cpp
            )

            add_executable(fusiondesk_pc_two_peer_clipboard_file_smoke
                tests/apps/pc/pc_two_peer_clipboard_file_smoke.cpp
            )

            add_executable(fusiondesk_pc_two_peer_clipboard_formatted_text_smoke
                tests/apps/pc/pc_two_peer_clipboard_formatted_text_smoke.cpp
            )

            add_executable(fusiondesk_pc_two_peer_clipboard_image_smoke
                tests/apps/pc/pc_two_peer_clipboard_image_smoke.cpp
            )

            add_executable(fusiondesk_pc_two_peer_clipboard_drag_smoke
                tests/apps/pc/pc_two_peer_clipboard_drag_smoke.cpp
            )

            add_executable(fusiondesk_pc_two_peer_clipboard_reconnect_smoke
                tests/apps/pc/pc_two_peer_clipboard_reconnect_smoke.cpp
            )

            target_compile_features(fusiondesk_pc_two_peer_clipboard_text_smoke
                PRIVATE
                    cxx_std_17
            )

            target_compile_features(fusiondesk_pc_two_peer_clipboard_file_smoke
                PRIVATE
                    cxx_std_17
            )

            target_compile_features(fusiondesk_pc_two_peer_clipboard_formatted_text_smoke
                PRIVATE
                    cxx_std_17
            )

            target_compile_features(fusiondesk_pc_two_peer_clipboard_image_smoke
                PRIVATE
                    cxx_std_17
            )

            target_compile_features(fusiondesk_pc_two_peer_clipboard_drag_smoke
                PRIVATE
                    cxx_std_17
            )

            target_compile_features(fusiondesk_pc_two_peer_clipboard_reconnect_smoke
                PRIVATE
                    cxx_std_17
            )

            target_compile_definitions(fusiondesk_pc_two_peer_clipboard_text_smoke
                PRIVATE
                    FUSIONDESK_PC_AGENT_EXE="$<TARGET_FILE:fusiondesk_pc_agent>"
                    FUSIONDESK_PC_CLIENT_EXE="$<TARGET_FILE:fusiondesk_pc_client>"
                    FUSIONDESK_PC_PROFILE_PLAN_EXE="$<TARGET_FILE:fusiondesk_pc_profile_plan>"
            )

            target_compile_definitions(fusiondesk_pc_two_peer_clipboard_file_smoke
                PRIVATE
                    FUSIONDESK_PC_AGENT_EXE="$<TARGET_FILE:fusiondesk_pc_agent>"
                    FUSIONDESK_PC_CLIENT_EXE="$<TARGET_FILE:fusiondesk_pc_client>"
                    FUSIONDESK_PC_PROFILE_PLAN_EXE="$<TARGET_FILE:fusiondesk_pc_profile_plan>"
            )

            target_compile_definitions(fusiondesk_pc_two_peer_clipboard_formatted_text_smoke
                PRIVATE
                    FUSIONDESK_PC_AGENT_EXE="$<TARGET_FILE:fusiondesk_pc_agent>"
                    FUSIONDESK_PC_CLIENT_EXE="$<TARGET_FILE:fusiondesk_pc_client>"
                    FUSIONDESK_PC_PROFILE_PLAN_EXE="$<TARGET_FILE:fusiondesk_pc_profile_plan>"
            )

            target_compile_definitions(fusiondesk_pc_two_peer_clipboard_image_smoke
                PRIVATE
                    FUSIONDESK_PC_AGENT_EXE="$<TARGET_FILE:fusiondesk_pc_agent>"
                    FUSIONDESK_PC_CLIENT_EXE="$<TARGET_FILE:fusiondesk_pc_client>"
                    FUSIONDESK_PC_PROFILE_PLAN_EXE="$<TARGET_FILE:fusiondesk_pc_profile_plan>"
            )

            target_compile_definitions(fusiondesk_pc_two_peer_clipboard_drag_smoke
                PRIVATE
                    FUSIONDESK_PC_AGENT_EXE="$<TARGET_FILE:fusiondesk_pc_agent>"
                    FUSIONDESK_PC_CLIENT_EXE="$<TARGET_FILE:fusiondesk_pc_client>"
                    FUSIONDESK_PC_PROFILE_PLAN_EXE="$<TARGET_FILE:fusiondesk_pc_profile_plan>"
            )

            target_compile_definitions(fusiondesk_pc_two_peer_clipboard_reconnect_smoke
                PRIVATE
                    FUSIONDESK_PC_AGENT_EXE="$<TARGET_FILE:fusiondesk_pc_agent>"
                    FUSIONDESK_PC_CLIENT_EXE="$<TARGET_FILE:fusiondesk_pc_client>"
                    FUSIONDESK_PC_PROFILE_PLAN_EXE="$<TARGET_FILE:fusiondesk_pc_profile_plan>"
            )

            add_dependencies(fusiondesk_pc_two_peer_clipboard_text_smoke
                fusiondesk_pc_agent
                fusiondesk_pc_client
                fusiondesk_pc_profile_plan
            )

            add_dependencies(fusiondesk_pc_two_peer_clipboard_file_smoke
                fusiondesk_pc_agent
                fusiondesk_pc_client
                fusiondesk_pc_profile_plan
            )

            add_dependencies(fusiondesk_pc_two_peer_clipboard_formatted_text_smoke
                fusiondesk_pc_agent
                fusiondesk_pc_client
                fusiondesk_pc_profile_plan
            )

            add_dependencies(fusiondesk_pc_two_peer_clipboard_image_smoke
                fusiondesk_pc_agent
                fusiondesk_pc_client
                fusiondesk_pc_profile_plan
            )

            add_dependencies(fusiondesk_pc_two_peer_clipboard_drag_smoke
                fusiondesk_pc_agent
                fusiondesk_pc_client
                fusiondesk_pc_profile_plan
            )

            add_dependencies(fusiondesk_pc_two_peer_clipboard_reconnect_smoke
                fusiondesk_pc_agent
                fusiondesk_pc_client
                fusiondesk_pc_profile_plan
            )

            target_link_libraries(fusiondesk_pc_two_peer_clipboard_text_smoke
                PRIVATE
                    ${FUSIONDESK_QT_LIBS}
            )

            target_link_libraries(fusiondesk_pc_two_peer_clipboard_file_smoke
                PRIVATE
                    ${FUSIONDESK_QT_LIBS}
            )

            target_link_libraries(fusiondesk_pc_two_peer_clipboard_formatted_text_smoke
                PRIVATE
                    ${FUSIONDESK_QT_LIBS}
            )

            target_link_libraries(fusiondesk_pc_two_peer_clipboard_image_smoke
                PRIVATE
                    ${FUSIONDESK_QT_LIBS}
            )

            target_link_libraries(fusiondesk_pc_two_peer_clipboard_drag_smoke
                PRIVATE
                    ${FUSIONDESK_QT_LIBS}
            )

            target_link_libraries(fusiondesk_pc_two_peer_clipboard_reconnect_smoke
                PRIVATE
                    ${FUSIONDESK_QT_LIBS}
            )

            add_test(
                NAME fusiondesk_pc_two_peer_clipboard_text_smoke
                COMMAND fusiondesk_pc_two_peer_clipboard_text_smoke
            )
            add_test(
                NAME fusiondesk_pc_two_peer_clipboard_file_smoke
                COMMAND fusiondesk_pc_two_peer_clipboard_file_smoke
            )
            add_test(
                NAME fusiondesk_pc_two_peer_clipboard_formatted_text_smoke
                COMMAND fusiondesk_pc_two_peer_clipboard_formatted_text_smoke
            )
            add_test(
                NAME fusiondesk_pc_two_peer_clipboard_image_smoke
                COMMAND fusiondesk_pc_two_peer_clipboard_image_smoke
            )
            add_test(
                NAME fusiondesk_pc_two_peer_clipboard_drag_smoke
                COMMAND fusiondesk_pc_two_peer_clipboard_drag_smoke
            )
            add_test(
                NAME fusiondesk_pc_two_peer_clipboard_reconnect_smoke
                COMMAND fusiondesk_pc_two_peer_clipboard_reconnect_smoke
            )
            find_program(FUSIONDESK_POWERSHELL_EXECUTABLE
                NAMES powershell pwsh
            )
            if(FUSIONDESK_POWERSHELL_EXECUTABLE)
                add_test(
                    NAME fusiondesk_clipboard_windows_validation_text_dryrun_smoke
                    COMMAND
                        ${FUSIONDESK_POWERSHELL_EXECUTABLE}
                        -NoProfile
                        -ExecutionPolicy
                        Bypass
                        -File
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/clipboard_windows_validation.ps1
                        -Mode
                        LocalNativeText
                        -Scenario
                        Text
                        -DryRun
                        -PackageRoot
                        $<TARGET_FILE_DIR:fusiondesk_pc_agent>
                        -LogDir
                        ${CMAKE_CURRENT_BINARY_DIR}/logs/clipboard_windows_validation_text_dryrun_smoke_$<CONFIG>
                )
                add_test(
                    NAME fusiondesk_clipboard_windows_validation_file_smoke
                    COMMAND
                        ${FUSIONDESK_POWERSHELL_EXECUTABLE}
                        -NoProfile
                        -ExecutionPolicy
                        Bypass
                        -File
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/clipboard_windows_validation.ps1
                        -Mode
                        LocalNativeText
                        -Scenario
                        File
                        -FileReadChunkBytes
                        17
                        -SaveFilesDir
                        ${CMAKE_CURRENT_BINARY_DIR}/logs/clipboard_windows_validation_file_smoke_$<CONFIG>/saved_files
                        -PackageRoot
                        $<TARGET_FILE_DIR:fusiondesk_pc_agent>
                        -LogDir
                        ${CMAKE_CURRENT_BINARY_DIR}/logs/clipboard_windows_validation_file_smoke_$<CONFIG>
                )
                add_test(
                    NAME fusiondesk_clipboard_windows_validation_image_dryrun_smoke
                    COMMAND
                        ${FUSIONDESK_POWERSHELL_EXECUTABLE}
                        -NoProfile
                        -ExecutionPolicy
                        Bypass
                        -File
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/clipboard_windows_validation.ps1
                        -Mode
                        LocalNativeText
                        -Scenario
                        Image
                        -DryRun
                        -PackageRoot
                        $<TARGET_FILE_DIR:fusiondesk_pc_agent>
                        -LogDir
                        ${CMAKE_CURRENT_BINARY_DIR}/logs/clipboard_windows_validation_image_dryrun_smoke_$<CONFIG>
                )
                add_test(
                    NAME fusiondesk_clipboard_windows_validation_rtf_dryrun_smoke
                    COMMAND
                        ${FUSIONDESK_POWERSHELL_EXECUTABLE}
                        -NoProfile
                        -ExecutionPolicy
                        Bypass
                        -File
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/clipboard_windows_validation.ps1
                        -Mode
                        LocalNativeText
                        -Scenario
                        Rtf
                        -DryRun
                        -PackageRoot
                        $<TARGET_FILE_DIR:fusiondesk_pc_agent>
                        -LogDir
                        ${CMAKE_CURRENT_BINARY_DIR}/logs/clipboard_windows_validation_rtf_dryrun_smoke_$<CONFIG>
                )
                add_test(
                    NAME fusiondesk_clipboard_windows_validation_drag_preflight_smoke
                    COMMAND
                        ${FUSIONDESK_POWERSHELL_EXECUTABLE}
                        -NoProfile
                        -ExecutionPolicy
                        Bypass
                        -File
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/clipboard_windows_validation.ps1
                        -Mode
                        LocalNativeText
                        -Scenario
                        DragPreflight
                        -PackageRoot
                        $<TARGET_FILE_DIR:fusiondesk_pc_agent>
                        -LogDir
                        ${CMAKE_CURRENT_BINARY_DIR}/logs/clipboard_windows_validation_drag_preflight_smoke_$<CONFIG>
                )
                set_tests_properties(
                    fusiondesk_clipboard_windows_validation_text_dryrun_smoke
                    fusiondesk_clipboard_windows_validation_file_smoke
                    fusiondesk_clipboard_windows_validation_image_dryrun_smoke
                    fusiondesk_clipboard_windows_validation_rtf_dryrun_smoke
                    fusiondesk_clipboard_windows_validation_drag_preflight_smoke
                    PROPERTIES TIMEOUT 45
                )
            endif()
            list(APPEND FUSIONDESK_TEST_TARGETS
                fusiondesk_pc_two_peer_clipboard_text_smoke
                fusiondesk_pc_two_peer_clipboard_file_smoke
                fusiondesk_pc_two_peer_clipboard_formatted_text_smoke
                fusiondesk_pc_two_peer_clipboard_image_smoke
                fusiondesk_pc_two_peer_clipboard_drag_smoke
                fusiondesk_pc_two_peer_clipboard_reconnect_smoke)
        endif()

        if(TARGET fusiondesk_qt_widget_display_adapters)
            target_compile_definitions(fusiondesk_pc_product_session_controller_tests
                PRIVATE
                    FUSIONDESK_PC_HAS_QT_IMAGE_DISPLAY=1
                    FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY=1
            )
            target_link_libraries(fusiondesk_pc_product_session_controller_tests
                PRIVATE
                    fusiondesk_qt_widget_display_adapters
                    ${FUSIONDESK_QT_WIDGETS_LIB}
            )
        endif()

        add_test(NAME fusiondesk_pc_client_smoke COMMAND fusiondesk_pc_client --smoke)
        add_test(NAME fusiondesk_pc_agent_smoke COMMAND fusiondesk_pc_agent --smoke)
        add_test(NAME fusiondesk_pc_agent_listen_profile_smoke COMMAND fusiondesk_pc_agent_listen_profile_smoke)
        add_test(NAME fusiondesk_pc_profile_plan_smoke COMMAND fusiondesk_pc_profile_plan_smoke)
        add_test(NAME fusiondesk_pc_feature_adapter_startup_smoke COMMAND fusiondesk_pc_feature_adapter_startup_smoke)
        add_test(NAME fusiondesk_pc_product_session_controller_tests COMMAND fusiondesk_pc_product_session_controller_tests)
        if(TARGET fusiondesk_platform_windows_display AND TARGET fusiondesk_qt_display_adapters)
            add_executable(fusiondesk_pc_agent_start_display_requires_ready_channel_smoke
                tests/apps/pc/pc_agent_start_display_requires_ready_channel_smoke.cpp
            )

            target_link_libraries(fusiondesk_pc_agent_start_display_requires_ready_channel_smoke
                PRIVATE
                    fusiondesk_pc_shell_common
            )

            add_executable(fusiondesk_pc_two_peer_start_display_smoke
                tests/apps/pc/pc_two_peer_start_display_smoke.cpp
            )

            add_executable(fusiondesk_pc_peer_profile_start_display_smoke
                tests/apps/pc/pc_peer_profile_start_display_smoke.cpp
            )

            target_compile_features(fusiondesk_pc_two_peer_start_display_smoke
                PRIVATE
                    cxx_std_17
            )

            target_compile_features(fusiondesk_pc_peer_profile_start_display_smoke
                PRIVATE
                    cxx_std_17
            )

            target_compile_definitions(fusiondesk_pc_two_peer_start_display_smoke
                PRIVATE
                    FUSIONDESK_PC_AGENT_EXE="$<TARGET_FILE:fusiondesk_pc_agent>"
                    FUSIONDESK_PC_CLIENT_EXE="$<TARGET_FILE:fusiondesk_pc_client>"
                    FUSIONDESK_PC_PROFILE_PLAN_EXE="$<TARGET_FILE:fusiondesk_pc_profile_plan>"
            )

            target_compile_definitions(fusiondesk_pc_peer_profile_start_display_smoke
                PRIVATE
                    FUSIONDESK_PC_AGENT_EXE="$<TARGET_FILE:fusiondesk_pc_agent>"
                    FUSIONDESK_PC_CLIENT_EXE="$<TARGET_FILE:fusiondesk_pc_client>"
                    FUSIONDESK_PC_PROFILE_PLAN_EXE="$<TARGET_FILE:fusiondesk_pc_profile_plan>"
            )

            add_dependencies(fusiondesk_pc_two_peer_start_display_smoke
                fusiondesk_pc_agent
                fusiondesk_pc_client
                fusiondesk_pc_profile_plan
            )

            add_dependencies(fusiondesk_pc_peer_profile_start_display_smoke
                fusiondesk_pc_agent
                fusiondesk_pc_client
                fusiondesk_pc_profile_plan
            )

            target_link_libraries(fusiondesk_pc_two_peer_start_display_smoke
                PRIVATE
                    ${FUSIONDESK_QT_LIBS}
            )

            target_link_libraries(fusiondesk_pc_peer_profile_start_display_smoke
                PRIVATE
                    ${FUSIONDESK_QT_LIBS}
            )

            add_test(
                NAME fusiondesk_pc_agent_start_display_requires_ready_channel_smoke
                COMMAND fusiondesk_pc_agent_start_display_requires_ready_channel_smoke
            )
            add_test(
                NAME fusiondesk_pc_two_peer_start_display_smoke
                COMMAND fusiondesk_pc_two_peer_start_display_smoke
            )
            add_test(
                NAME fusiondesk_pc_peer_profile_start_display_smoke
                COMMAND fusiondesk_pc_peer_profile_start_display_smoke
            )
            add_test(NAME fusiondesk_pc_client_display_mount_smoke COMMAND fusiondesk_pc_client --smoke --mount-display)
            add_test(NAME fusiondesk_pc_agent_display_mount_smoke COMMAND fusiondesk_pc_agent --smoke --mount-display)
            if(TARGET fusiondesk_qt_widget_display_adapters)
                add_test(
                    NAME fusiondesk_pc_client_visible_display_mount_smoke
                    COMMAND fusiondesk_pc_client --smoke --show-display-window
                )
            endif()
            list(APPEND FUSIONDESK_TEST_TARGETS
                fusiondesk_pc_agent_start_display_requires_ready_channel_smoke
                fusiondesk_pc_two_peer_start_display_smoke
                fusiondesk_pc_peer_profile_start_display_smoke)
        endif()
        list(APPEND FUSIONDESK_TEST_TARGETS
            fusiondesk_pc_client
            fusiondesk_pc_agent
            fusiondesk_pc_profile_plan
            fusiondesk_pc_agent_listen_profile_smoke
            fusiondesk_pc_profile_plan_smoke
            fusiondesk_pc_feature_adapter_startup_smoke
            fusiondesk_pc_product_session_controller_tests)
    else()
        message(STATUS "Qt adapters disabled because Qt Core/Network was not found")
    endif()
endif()

foreach(test_target IN LISTS FUSIONDESK_TEST_TARGETS)
    if(MSVC)
        target_compile_options(${test_target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
endforeach()
