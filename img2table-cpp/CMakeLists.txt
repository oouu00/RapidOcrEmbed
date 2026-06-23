cmake_minimum_required(VERSION 3.16)
project(RapidOcrEmbed VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OpenCV_STATIC ON)
set(BUILD_SHARED_LIBS OFF)

include("${CMAKE_SOURCE_DIR}/../opencv-static/OpenCVWrapperConfig.cmake")
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

file(GLOB TABLE_SOURCES src/table/*.cpp)
file(GLOB TABLE_HEADERS include/table/*.h)

add_library(table_detect STATIC ${TABLE_SOURCES} ${TABLE_HEADERS})
target_include_directories(table_detect PUBLIC include)
target_link_libraries(table_detect PUBLIC ${OpenCV_LIBS})

add_executable(table_test test/main.cpp)
target_link_libraries(table_test PRIVATE table_detect ${OpenCV_LIBS})

set_target_properties(table_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
