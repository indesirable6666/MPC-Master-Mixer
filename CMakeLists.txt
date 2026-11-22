cmake_minimum_required(VERSION 3.16)

project(MPCMasterMixerHostTests LANGUAGES CXX)

# This is deliberately a host-side validation harness, not an application
# build or a target-device deployment configuration.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

function(mpc_add_host_test target source)
    add_executable(${target} ${source})
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Werror -pedantic
    )
    add_test(NAME ${target} COMMAND ${target})
endfunction()

mpc_add_host_test(audio_engine_test tests/audio_engine_test.cpp)
mpc_add_host_test(audio_engine_solo_arbitration_test tests/audio_engine_solo_arbitration_test.cpp)
target_include_directories(audio_engine_test PRIVATE src/audio)
mpc_add_host_test(audio_insert_frequency_response_test tests/audio_insert_frequency_response_test.cpp)
mpc_add_host_test(audio_eq_frequency_response_test tests/audio_eq_frequency_response_test.cpp)
mpc_add_host_test(audio_insert_chain_test tests/audio_insert_chain_test.cpp)
mpc_add_host_test(audio_eq_insert_chain_test tests/audio_eq_insert_chain_test.cpp)
mpc_add_host_test(audio_aux_send_return_test tests/audio_aux_send_return_test.cpp)
mpc_add_host_test(audio_aux_control_bridge_test tests/audio_aux_control_bridge_test.cpp)
mpc_add_host_test(audio_control_bridge_queue_lifecycle_test tests/audio_control_bridge_queue_lifecycle_test.cpp)
mpc_add_host_test(audio_insert_replacement_ticket_test tests/audio_insert_replacement_ticket_test.cpp)

mpc_add_host_test(audio_block_meter_test tests/audio_block_meter_test.cpp)
mpc_add_host_test(audio_meter_snapshot_bridge_test tests/audio_meter_snapshot_bridge_test.cpp)
mpc_add_host_test(audio_meter_snapshot_freshness_test tests/audio_meter_snapshot_freshness_test.cpp)
mpc_add_host_test(audio_duplex_meter_tap_test tests/audio_duplex_meter_tap_test.cpp)
mpc_add_host_test(audio_input_pair_host_test tests/audio_input_pair_host_test.cpp)
mpc_add_host_test(audio_meter_ui_consumer_test tests/audio_meter_ui_consumer_test.cpp)
mpc_add_host_test(audio_meter_clip_reset_test tests/audio_meter_clip_reset_test.cpp)
mpc_add_host_test(meter_presentation_adapter_test tests/meter_presentation_adapter_test.cpp)

mpc_add_host_test(audio_meter_end_to_end_test tests/audio_meter_end_to_end_test.cpp)
target_include_directories(audio_meter_end_to_end_test PRIVATE src/audio)

mpc_add_host_test(audio_native_signal_scenarios_test tests/audio_native_signal_scenarios_test.cpp)
mpc_add_host_test(audio_signal_reference_test tests/audio_signal_reference_test.cpp)
set_tests_properties(audio_signal_reference_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

mpc_add_host_test(meter_model_test tests/meter_model_test.cpp)
mpc_add_host_test(mixer_controller_test tests/mixer_controller_test.cpp)
mpc_add_host_test(mixer_controller_state_sequence_test tests/mixer_controller_state_sequence_test.cpp)
mpc_add_host_test(mixer_mute_solo_order_test tests/mixer_mute_solo_order_test.cpp)
mpc_add_host_test(mixer_presentation_snapshot_test tests/mixer_presentation_snapshot_test.cpp)
mpc_add_host_test(mixer_presentation_all_tracks_test tests/mixer_presentation_all_tracks_test.cpp)
mpc_add_host_test(mixer_ui_command_dispatcher_test tests/mixer_ui_command_dispatcher_test.cpp)
mpc_add_host_test(midi_adapter_test tests/midi_adapter_test.cpp)
target_include_directories(midi_adapter_test PRIVATE src/midi src/mixer)

option(MPC_BUILD_HOST_BENCHMARK "Build the non-gating host DSP benchmark" ON)
if(MPC_BUILD_HOST_BENCHMARK)
    add_executable(audio_engine_benchmark tools/audio-engine-benchmark.cpp)
    target_compile_options(audio_engine_benchmark PRIVATE
        -O3 -DNDEBUG -Wall -Wextra -Werror -pedantic
    )
endif()
