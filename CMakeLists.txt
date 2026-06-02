cmake_minimum_required(VERSION 3.20)
project(b70tools CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

set(B70_SOURCES
    src/main.cc
    src/schema/enums_to_string.cc
    src/schema/json_emit.cc
    src/schema/jsonl_writer.cc
    src/schema/delta_filter.cc
    src/schema/compact_format.cc
    src/bus/event_bus.cc

    src/collectors/fake_collector.cc
    src/arbitrator/adapter_state_fsm.cc
    src/arbitrator/disagreement_rules.cc
    src/runtime/poll_loop.cc
    src/runtime/watchdog.cc
    src/runtime/session.cc
    src/runtime/replay_reader.cc

    src/identity/dxgi_enum.cc
    src/identity/setupapi_devnodes.cc
    src/identity/vulkan_enum.cc
    src/identity/reconciler.cc

    src/collectors/d3dkmt_adapter_perfdata.cc
    src/collectors/dxgi_query_video_memory.cc
    src/collectors/vulkan_memory_budget.cc
    src/collectors/igcl_power_telemetry.cc
    src/collectors/host_memory.cc
    src/collectors/pdh_gpu_memory.cc
    src/collectors/pdh_gpu_engine.cc

    src/tools/verdict.cc

    src/runtime/library_audit.cc
    src/runtime/driver_fingerprint.cc

    src/tools/summarize.cc
    src/tools/disagreements.cc
    src/tools/adapters.cc
    src/tools/self.cc
)

# NOTE: src/collectors/d3dkmt_query_statistics.cc was attempted but the
# Win10.0.19045 kernel returns STATUS_INVALID_PARAMETER for our struct
# layout (Type 14 isn't supported; outer struct missing trailing input
# discriminator union). Replaced by pdh_gpu_memory.cc. Source retained
# for future Win11/WDDM 2.7+ work.

add_executable(b70tools ${B70_SOURCES})
target_include_directories(b70tools PRIVATE
    src
    third_party/Vulkan-Headers/include
    third_party/igcl/include
)
target_link_libraries(b70tools PRIVATE
    dxgi
    dxguid
    setupapi
    cfgmgr32
    pdh
)
