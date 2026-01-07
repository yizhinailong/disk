cmake_minimum_required(VERSION 4.0)
project(disk-test CXX)

add_executable(${PROJECT_NAME} test_main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)

ParseAndAddDrogonTests(${PROJECT_NAME})
