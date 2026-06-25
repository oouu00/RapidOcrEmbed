if (WIN32)
    cmake_minimum_required(VERSION 3.12)
elseif (APPLE)
    cmake_minimum_required(VERSION 3.17)
elseif (UNIX)
    cmake_minimum_required(VERSION 3.17)
endif ()
project(RapidOcrOnnx)

# Output BIN JNI CLIB
if (NOT DEFINED OCR_OUTPUT)
    set(OCR_OUTPUT "BIN")
    message(STATUS "No OCR_OUTPUT, defaulting to BIN")
endif ()
option(OCR_BENCHMARK "build benchmark" ON)
set(OCR_BENCHMARK ON)
#set(OCR_OUTPUT "BIN")

option(OCR_EMBEDDED_MODELS "embed models into DLL (single file distribution)" OFF)
mark_as_advanced(OCR_EMBEDDED_MODELS)

if (NOT DEFINED OCR_ONNX)
    set(OCR_ONNX "CPU")
    message(STATUS "No OCR_ONNX, defaulting to CPU")
endif ()
#set(OCR_OUTPUT "GPU")

set(CMAKE_CXX_STANDARD 17)
# ONNX Runtime 1.26.0 headers use dynamic exception specifications not allowed in strict C++17
add_compile_options(/permissive)
add_definitions(-DUNICODE -D_UNICODE)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions("-Wall -g -O0")
else ()
    add_definitions("-Wall")
endif ()

# MNN (OCR models: det, cls, rec)
set(MNN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/MNN_lib)
if (NOT EXISTS ${MNN_DIR}/lib/MNN.lib)
    message(FATAL_ERROR "MNN.lib not found at ${MNN_DIR}/lib/MNN.lib")
endif ()
message(STATUS "MNN_LIB: ${MNN_DIR}/lib/MNN.lib")
message(STATUS "MNN_INCLUDE: ${MNN_DIR}/include")
include_directories(${MNN_DIR}/include)

# OnnxRuntime (table model only)
if (OCR_ONNX STREQUAL "CPU")
    include(${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime-static/OnnxRuntimeWrapper.cmake)
elseif (OCR_ONNX STREQUAL "CUDA") # CUDA
    include(${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime-gpu/OnnxRuntimeWrapper.cmake)
endif ()
find_package(OnnxRuntime REQUIRED)
if (OnnxRuntime_FOUND)
    message(STATUS "OnnxRuntime_LIBS: ${OnnxRuntime_LIBS}")
    message(STATUS "OnnxRuntime_INCLUDE_DIRS: ${OnnxRuntime_INCLUDE_DIRS}")
    include_directories(${OnnxRuntime_INCLUDE_DIRS})
else ()
    message(FATAL_ERROR "onnxruntime Not Found!")
endif (OnnxRuntime_FOUND)

# OpenCV
set(BUILD_SHARED_LIBS false)
include(${CMAKE_CURRENT_SOURCE_DIR}/opencv-static/OpenCVWrapperConfig.cmake)
find_package(OpenCV REQUIRED)
if (OpenCV_FOUND)
    message(STATUS "OpenCV_LIBS: ${OpenCV_LIBS}")
    message(STATUS "OpenCV_INCLUDE_DIRS: ${OpenCV_INCLUDE_DIRS}")
else ()
    message(FATAL_ERROR "opencv Not Found!")
endif (OpenCV_FOUND)

# project include
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Embedded models support
if (OCR_EMBEDDED_MODELS)
    message(STATUS "Embedded models: ON")
    add_definitions(-D__EMBEDDED_MODELS__)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/models)
    # Add table model embedding
    file(GLOB TABLE_MODEL_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/models/model_table*.h)
    if (TABLE_MODEL_HEADERS)
        message(STATUS "Table model headers found")
    endif ()
else ()
    message(STATUS "Embedded models: OFF")
endif ()

# source
file(GLOB OCR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
set(OCR_COMPILE_CODE ${OCR_SRC})

# img2table-cpp (bordered table detection, no model needed)
file(GLOB IMG2TABLE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/img2table-cpp/src/table/*.cpp)
list(APPEND OCR_COMPILE_CODE ${IMG2TABLE_SRC})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/img2table-cpp/include)

# JNI
if (OCR_OUTPUT STREQUAL "JNI")
    find_package(JNI REQUIRED)
    if (JNI_FOUND)
        message("JNI FOUND")
        message(STATUS "JNI_LIBS: ${JNI_LIBS}")
        message(STATUS "JNI_INCLUDE_DIRS: ${JNI_INCLUDE_DIRS}")
        include_directories(${JNI_INCLUDE_DIRS})
    else ()
        message(FATAL_ERROR "JNI Not Found!")
    endif ()
endif ()

set(MNN_LIB ${MNN_DIR}/lib/MNN.lib)

# ⚠️ 链接顺序重要: MNN 必须在 OnnxRuntime_LIBS 前面
# 原因: MNN 和 ORT 都内置 flatbuffers 库, 有 21 个符号冲突
#   (Allocator vtable, DefaultAllocator, ReadScalar, EndianScalar 等)
# MSVC 解析重复符号取第一个定义, MNN 必须先链接以确保 flatbuffers 符号正确
# 同时 OnnxRuntimeConfig.cmake 中已排除 flatbuffers.lib 避免重复
# 参见: OnnxRuntimeConfig.cmake 第 16-17 行

if (OCR_OUTPUT STREQUAL "JNI") # JNI
    add_library(RapidOcrOnnx SHARED ${OCR_COMPILE_CODE})
    target_compile_definitions(RapidOcrOnnx PRIVATE __JNI__)
    target_link_libraries(RapidOcrOnnx ${MNN_LIB} ${OnnxRuntime_LIBS} ${OpenCV_LIBS} ${JNI_LIBS})
elseif (OCR_OUTPUT STREQUAL "CLIB") # CLIB (DLL + Static Lib)
    # 动态库 (DLL) — OUTPUT_NAME 与静态库统一为 RapidOcrOnnxStatic
    add_library(RapidOcrOnnx SHARED ${OCR_COMPILE_CODE})
    set_target_properties(RapidOcrOnnx PROPERTIES OUTPUT_NAME "RapidOcrOnnxStatic")
    target_compile_definitions(RapidOcrOnnx PRIVATE __CLIB__)
    target_link_libraries(RapidOcrOnnx ${MNN_LIB} ${OnnxRuntime_LIBS} ${OpenCV_LIBS})

    # 静态库 (LIB)
    add_library(RapidOcrOnnxStatic STATIC ${OCR_COMPILE_CODE})
    target_compile_definitions(RapidOcrOnnxStatic PRIVATE __CLIB__)
    target_link_libraries(RapidOcrOnnxStatic ${MNN_LIB} ${OnnxRuntime_LIBS} ${OpenCV_LIBS})
elseif (OCR_OUTPUT STREQUAL "BIN") # BIN
    add_executable(RapidOcrOnnx ${OCR_COMPILE_CODE})
    target_compile_definitions(RapidOcrOnnx PRIVATE __EXEC__)
    target_link_libraries(RapidOcrOnnx ${MNN_LIB} ${OnnxRuntime_LIBS} ${OpenCV_LIBS})
endif ()

install(TARGETS RapidOcrOnnx EXPORT RapidOcrOnnx)
if (OCR_OUTPUT STREQUAL "CLIB") # CLIB
    # 安装静态库
    install(TARGETS RapidOcrOnnxStatic EXPORT RapidOcrOnnxStatic
            ARCHIVE DESTINATION lib)

    file(GLOB OCR_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h)
    install(FILES ${OCR_INCLUDE} DESTINATION include)

    if (OCR_EMBEDDED_MODELS)
        file(GLOB OCR_MODEL_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include/models/*.h)
        install(FILES ${OCR_MODEL_INCLUDE} DESTINATION include/models)
    endif ()
endif ()

if (OCR_ONNX STREQUAL "CUDA")
    target_compile_definitions(RapidOcrOnnx PRIVATE __CUDA__)
endif ()

# benchmark
if (OCR_BENCHMARK AND (OCR_OUTPUT STREQUAL "BIN"))
    add_executable(benchmark benchmark/benchmark.cpp
            src/clipper.cpp
            src/CrnnNet.cpp
            src/DbNet.cpp
            src/getopt.cpp
            src/OcrLite.cpp
            src/OcrUtils.cpp
            src/TbpuLayout.cpp)
    target_link_libraries(benchmark ${MNN_LIB} ${OnnxRuntime_LIBS} ${OpenCV_LIBS})
    target_compile_definitions(benchmark PRIVATE __EXEC__)

    if (OCR_ONNX STREQUAL "CUDA")
        target_compile_definitions(benchmark PRIVATE __CUDA__)
    endif ()
    install(TARGETS benchmark EXPORT benchmark
            ARCHIVE DESTINATION staticlib
            LIBRARY DESTINATION sharedlib
            RUNTIME DESTINATION bin)
endif ()

# Windows Link CRT
if (OCR_BUILD_CRT STREQUAL "True")
    include(${CMAKE_CURRENT_SOURCE_DIR}/OcrCRTLinkage.cmake)
endif ()