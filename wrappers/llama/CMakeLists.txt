cmake_minimum_required(VERSION 3.21)
project(qvw_llama_wrap LANGUAGES CXX)

include(GNUInstallDirs)
include(FetchContent)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QVW_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")
set(QVW_LLAMA_CPP_TAG "b8095")
set(QVW_GPU_BACKEND "NONE" CACHE STRING "GPU backend for GGML: NONE, VULKAN/VULCAN, CUDA, METAL, ROCM")
set_property(CACHE QVW_GPU_BACKEND PROPERTY STRINGS NONE VULKAN VULCAN CUDA METAL ROCM)

string(TOUPPER "${QVW_GPU_BACKEND}" QVW_GPU_BACKEND_UPPER)

set(QVW_GGML_CUDA OFF)
set(QVW_GGML_HIP OFF)
set(QVW_GGML_VULKAN OFF)
set(QVW_GGML_METAL OFF)

if(QVW_GPU_BACKEND_UPPER STREQUAL "NONE")
  # CPU-only build
elseif(QVW_GPU_BACKEND_UPPER STREQUAL "VULCAN")
  set(QVW_GGML_VULKAN ON)
elseif(QVW_GPU_BACKEND_UPPER STREQUAL "VULKAN")
  set(QVW_GGML_VULKAN ON)
elseif(QVW_GPU_BACKEND_UPPER STREQUAL "CUDA")
  set(QVW_GGML_CUDA ON)
elseif(QVW_GPU_BACKEND_UPPER STREQUAL "METAL")
  set(QVW_GGML_METAL ON)
elseif(QVW_GPU_BACKEND_UPPER STREQUAL "ROCM")
  set(QVW_GGML_HIP ON)
else()
  message(FATAL_ERROR "Invalid QVW_GPU_BACKEND='${QVW_GPU_BACKEND}'. Expected one of: NONE, VULKAN/VULCAN, CUDA, METAL, ROCM")
endif()

message(STATUS "qvw_llama_wrap: QVW_GPU_BACKEND=${QVW_GPU_BACKEND_UPPER}")

FetchContent_Declare(
  llama_cpp
  GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
  GIT_TAG ${QVW_LLAMA_CPP_TAG}
)

# keep wrapper self-contained and minimal
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# option names vary across revisions; set the common intent:
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)

set(GGML_CUDA     ${QVW_GGML_CUDA} CACHE BOOL "" FORCE)
set(GGML_HIP      ${QVW_GGML_HIP} CACHE BOOL "" FORCE)
set(GGML_VULKAN   ${QVW_GGML_VULKAN} CACHE BOOL "" FORCE)
set(GGML_METAL    ${QVW_GGML_METAL} CACHE BOOL "" FORCE)
set(GGML_OPENCL   OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(llama_cpp)

add_library(qvw_llama_wrap SHARED
  src/LlamaEngine.cpp
  ${QVW_ROOT_DIR}/include/qvw/EngineBase.h
  ${QVW_ROOT_DIR}/include/qvw/LlamaEngine.h
  ${QVW_ROOT_DIR}/include/qvw/log_wrapper.h
)

target_compile_definitions(qvw_llama_wrap PRIVATE
  QVW_LLAMA_WRAP_BUILD
  QVW_LLAMA_CPP_TAG=\"${QVW_LLAMA_CPP_TAG}\"
)
target_compile_options(qvw_llama_wrap PRIVATE -frtti)

target_include_directories(qvw_llama_wrap
  PUBLIC
    ${QVW_ROOT_DIR}/include
)

set_target_properties(qvw_llama_wrap PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)

if(UNIX AND NOT APPLE)
  target_link_options(qvw_llama_wrap PRIVATE "-Wl,--exclude-libs,ALL")
  target_link_options(qvw_llama_wrap PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/exports.map"
  )
endif()

# link llama target (name differs across revisions)
if(TARGET llama)
  target_link_libraries(qvw_llama_wrap PRIVATE llama)
elseif(TARGET llama_lib)
  target_link_libraries(qvw_llama_wrap PRIVATE llama_lib)
elseif(TARGET ggml::llama)
  target_link_libraries(qvw_llama_wrap PRIVATE ggml::llama)
else()
  message(FATAL_ERROR "Cannot find llama.cpp library target to link")
endif()
