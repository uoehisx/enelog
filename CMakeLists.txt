cmake_minimum_required(VERSION 3.22)
project(enelog C)

set(CMAKE_C_STANDARD 11)

option(USE_NVML "Build with NVIDIA NVML support" ON)

add_executable(enelog enelog.c)

if(USE_NVML)
    message(STATUS "NVML support enabled")

    set(CUDA_ROOT "/usr/local/cuda" CACHE PATH "Path to CUDA Toolkit")

    find_path(NVML_INCLUDE_DIR
            NAMES nvml.h
            HINTS "${CUDA_ROOT}/include" "/usr/local/cuda/include" "/usr/include"
            REQUIRED
    )

    find_library(NVML_LIBRARY
            NAMES nvidia-ml
            HINTS "/usr/lib/x86_64-linux-gnu" "${CUDA_ROOT}/lib64" "${CUDA_ROOT}/lib64/stubs"
            REQUIRED
    )

    target_include_directories(enelog PRIVATE "${NVML_INCLUDE_DIR}")
    target_link_libraries(enelog PRIVATE "${NVML_LIBRARY}")
else()
    message(STATUS "NVML support disabled")
    target_compile_definitions(enelog PRIVATE NO_NVML)
endif()