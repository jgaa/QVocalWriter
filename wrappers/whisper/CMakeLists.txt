cmake_minimum_required(VERSION 3.21)
project(qvw_whisper_wrap LANGUAGES CXX)

include(GNUInstallDirs)
include(FetchContent)

set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QVW_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../")
set(QVW_GPU_BACKEND "NONE" CACHE STRING "GPU backend for GGML: NONE, VULKAN/VULCAN, CUDA, METAL, ROCM")
set_property(CACHE QVW_GPU_BACKEND PROPERTY STRINGS NONE VULKAN VULCAN CUDA METAL ROCM)

string(TOUPPER "${QVW_GPU_BACKEND}" QVW_GPU_BACKEND_UPPER)
set(QVW_GPU_BACKEND_EFFECTIVE "${QVW_GPU_BACKEND_UPPER}")

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
  find_package(hip QUIET CONFIG)
  find_package(rocblas QUIET CONFIG)
  find_package(hipblas QUIET CONFIG)
  find_program(QVW_HIPCC_EXECUTABLE NAMES hipcc)
  find_library(QVW_ROCBLAS_LIBRARY NAMES rocblas)
  find_library(QVW_HIPBLAS_LIBRARY NAMES hipblas)
  if((NOT hip_FOUND AND NOT QVW_HIPCC_EXECUTABLE)
     OR (NOT rocblas_FOUND AND NOT QVW_ROCBLAS_LIBRARY)
     OR (NOT hipblas_FOUND AND NOT QVW_HIPBLAS_LIBRARY))
    message(WARNING
      "qvw_whisper_wrap: ROCM backend requested, but required ROCm/HIP components were not found "
      "(need HIP + rocBLAS + hipBLAS). Falling back to CPU-only backend (NONE).")
    set(QVW_GPU_BACKEND_EFFECTIVE "NONE")
  endif()

  if(QVW_GPU_BACKEND_EFFECTIVE STREQUAL "ROCM")
    set(QVW_GGML_HIP ON)
  endif()
else()
  message(FATAL_ERROR "Invalid QVW_GPU_BACKEND='${QVW_GPU_BACKEND}'. Expected one of: NONE, VULKAN/VULCAN, CUDA, METAL, ROCM")
endif()

message(STATUS "qvw_whisper_wrap: QVW_GPU_BACKEND requested=${QVW_GPU_BACKEND_UPPER}, effective=${QVW_GPU_BACKEND_EFFECTIVE}")

# ---- Fetch whisper privately (only inside wrapper build) ----
FetchContent_Declare(
  whisper_cpp
  GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
  GIT_TAG v1.8.3
)

# Build whisper minimal / CPU-only
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)

set(GGML_CUDA ${QVW_GGML_CUDA} CACHE BOOL "" FORCE)
set(GGML_HIP ${QVW_GGML_HIP} CACHE BOOL "" FORCE)
set(GGML_VULKAN ${QVW_GGML_VULKAN} CACHE BOOL "" FORCE)
set(GGML_METAL ${QVW_GGML_METAL} CACHE BOOL "" FORCE)
set(GGML_SYCL OFF CACHE BOOL "" FORCE)
set(GGML_OPENCL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(whisper_cpp)

# ---- Wrapper library ----
add_library(qvw_whisper_wrap SHARED
  src/WhisperEngine.cpp
  ${QVW_ROOT_DIR}/include/qvw/EngineBase.h
  ${QVW_ROOT_DIR}/include/qvw/WhisperEngine.h
  ${QVW_ROOT_DIR}/include/qvw/log_wrapper.h
  src/WhisperCtx.cpp
)

target_compile_definitions(qvw_whisper_wrap PRIVATE QVW_WHISPER_WRAP_BUILD)
target_compile_options(qvw_whisper_wrap PRIVATE -frtti)

target_include_directories(qvw_whisper_wrap
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PUBLIC
    ${QVW_ROOT_DIR}/include # Interfaces
)

# Hide everything by default; export only what you mark visible in header
set_target_properties(qvw_whisper_wrap PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)

# Avoid exporting symbols from static libs linked into this wrapper (Linux)
if(UNIX AND NOT APPLE)
  target_link_options(qvw_whisper_wrap PRIVATE "-Wl,--exclude-libs,ALL")

  target_link_options(qvw_whisper_wrap PRIVATE
      "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/exports.map"
    )

  # If toolchain adds --gc-sections, don't allow it to drop everything
  #target_link_options(qvw_whisper_wrap PRIVATE "-Wl,--no-gc-sections")
endif()

target_link_libraries(qvw_whisper_wrap PRIVATE whisper)

# ---- Install ----
install(TARGETS qvw_whisper_wrap
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  COMPONENT qvw_whisper_wrap
)

install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  COMPONENT qvw_whisper_wrap
)
