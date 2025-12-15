cmake_minimum_required(VERSION 3.21)
project(qvw_whisper_wrap LANGUAGES CXX)

include(GNUInstallDirs)
include(FetchContent)

set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)

set(QVW_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../")

# ---- Fetch whisper privately (only inside wrapper build) ----
FetchContent_Declare(
  whisper_cpp
  GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
  GIT_TAG v1.8.2
)

# Build whisper minimal / CPU-only
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)

set(GGML_CUDA OFF CACHE BOOL "" FORCE)
set(GGML_HIPBLAS OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)
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

target_compile_features(qvw_whisper_wrap PUBLIC cxx_std_20)

target_compile_definitions(qvw_whisper_wrap PRIVATE QVW_WHISPER_WRAP_BUILD)


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
