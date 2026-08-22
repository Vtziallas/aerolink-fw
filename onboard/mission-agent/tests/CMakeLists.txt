include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.6.0
)
FetchContent_MakeAvailable(Catch2)

find_package(Threads REQUIRED)

add_executable(mission_agent_tests test_smoke.cpp test_protocol.cpp test_state_tracker.cpp test_command_validator.cpp test_replay_guard.cpp test_agent_server.cpp test_telemetry_publisher.cpp ../src/protocol.cpp ../src/state_tracker.cpp ../src/command_validator.cpp ../src/replay_guard.cpp ../src/agent_server.cpp ../src/telemetry_publisher.cpp)
target_link_libraries(mission_agent_tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)
target_compile_options(mission_agent_tests PRIVATE ${MISSION_AGENT_WARNINGS})

include(CTest)
include(Catch)
catch_discover_tests(mission_agent_tests)
