include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.6.0
)
FetchContent_MakeAvailable(Catch2)

find_package(Threads REQUIRED)

add_executable(mission_agent_tests test_smoke.cpp test_protocol.cpp test_state_tracker.cpp ../src/protocol.cpp ../src/state_tracker.cpp)
target_link_libraries(mission_agent_tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json Threads::Threads)

include(CTest)
include(Catch)
catch_discover_tests(mission_agent_tests)
