cmake_minimum_required(VERSION 3.16)
project(mission_agent CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(MAVSDK REQUIRED)
find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# -Wall -Wextra on every target we own. Notably this makes glibc's
# warn_unused_result on write()/read() an actual warning -- discarded write()
# return values were the root of the SIGPIPE-crash and blocking-write bugs
# found in the final review.
set(MISSION_AGENT_WARNINGS -Wall -Wextra)

add_executable(mission_agent src/main.cpp src/protocol.cpp src/state_tracker.cpp src/command_validator.cpp src/replay_guard.cpp src/mavlink_connection.cpp src/agent_server.cpp src/telemetry_publisher.cpp)
target_link_libraries(mission_agent PRIVATE nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)
target_compile_options(mission_agent PRIVATE ${MISSION_AGENT_WARNINGS})

add_executable(manual_mavlink_test tests/manual_mavlink_test.cpp src/protocol.cpp src/state_tracker.cpp src/mavlink_connection.cpp)
target_link_libraries(manual_mavlink_test PRIVATE nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)
target_compile_options(manual_mavlink_test PRIVATE ${MISSION_AGENT_WARNINGS})

enable_testing()
add_subdirectory(tests)
