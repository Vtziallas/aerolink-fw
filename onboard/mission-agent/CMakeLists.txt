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

add_executable(mission_agent src/main.cpp src/protocol.cpp src/state_tracker.cpp src/command_validator.cpp src/mavlink_connection.cpp src/agent_server.cpp)
target_link_libraries(mission_agent PRIVATE nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)

add_executable(manual_mavlink_test tests/manual_mavlink_test.cpp src/protocol.cpp src/state_tracker.cpp src/mavlink_connection.cpp)
target_link_libraries(manual_mavlink_test PRIVATE nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)

enable_testing()
add_subdirectory(tests)
