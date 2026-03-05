cmake_minimum_required(VERSION 3.16)


project(QVocalWriter VERSION 0.1.0 LANGUAGES CXX)

add_definitions(-DAPP_VERSION=\"${CMAKE_PROJECT_VERSION}\")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(QVW_GPU_BACKEND "VULKAN" CACHE STRING "GPU backend for whisper/llama wrappers (NONE, VULKAN/VULCAN, CUDA, METAL, ROCM)")
set_property(CACHE QVW_GPU_BACKEND PROPERTY STRINGS NONE VULKAN VULCAN CUDA METAL ROCM)

string(TOUPPER "${QVW_GPU_BACKEND}" QVW_GPU_BACKEND_UPPER)
if(QVW_GPU_BACKEND_UPPER STREQUAL "VULCAN")
  set(QVW_GPU_BACKEND_UPPER "VULKAN")
endif()

set(QVW_GPU_BACKEND_EFFECTIVE "${QVW_GPU_BACKEND_UPPER}")

if(NOT QVW_GPU_BACKEND_UPPER MATCHES "^(NONE|VULKAN|CUDA|METAL|ROCM)$")
  message(FATAL_ERROR "Invalid QVW_GPU_BACKEND='${QVW_GPU_BACKEND}'. Expected one of: NONE, VULKAN/VULCAN, CUDA, METAL, ROCM")
endif()

if(QVW_GPU_BACKEND_UPPER STREQUAL "VULKAN")
  find_package(Vulkan COMPONENTS glslc QUIET)
  if(NOT Vulkan_FOUND)
    message(WARNING
      "QVW_GPU_BACKEND=VULKAN requested, but Vulkan headers and/or glslc were not found. "
      "Falling back to CPU-only backend (NONE).")
    set(QVW_GPU_BACKEND_EFFECTIVE "NONE")
  endif()
elseif(QVW_GPU_BACKEND_UPPER STREQUAL "ROCM")
  set(QVW_ROCM_AVAILABLE TRUE)
  find_package(hip QUIET CONFIG)
  find_package(rocblas QUIET CONFIG)
  find_package(hipblas QUIET CONFIG)
  find_program(QVW_HIPCC_EXECUTABLE NAMES hipcc)
  find_library(QVW_ROCBLAS_LIBRARY NAMES rocblas)
  find_library(QVW_HIPBLAS_LIBRARY NAMES hipblas)
  if((NOT hip_FOUND AND NOT QVW_HIPCC_EXECUTABLE)
     OR (NOT rocblas_FOUND AND NOT QVW_ROCBLAS_LIBRARY)
     OR (NOT hipblas_FOUND AND NOT QVW_HIPBLAS_LIBRARY))
    set(QVW_ROCM_AVAILABLE FALSE)
  endif()

  if(NOT QVW_ROCM_AVAILABLE)
    message(WARNING
      "QVW_GPU_BACKEND=ROCM requested, but required ROCm/HIP components were not found "
      "(need HIP + rocBLAS + hipBLAS; detected via package config or system libraries). "
      "Falling back to CPU-only backend (NONE).")
    set(QVW_GPU_BACKEND_EFFECTIVE "NONE")
  endif()
endif()

message(STATUS "QVW GPU backend requested: ${QVW_GPU_BACKEND_UPPER}, effective: ${QVW_GPU_BACKEND_EFFECTIVE}")

set(QVW_GPU_BACKEND_AVAILABLE 0)
if(NOT QVW_GPU_BACKEND_EFFECTIVE STREQUAL "NONE")
  set(QVW_GPU_BACKEND_AVAILABLE 1)
endif()

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTDBUS  OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTNETWORK ON CACHE BOOL "" FORCE)
set(QCORO_WITH_QTWEBSOCKETS OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTQUICK OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QML     OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTTEST  OFF CACHE BOOL "" FORCE)
set(QCORO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(QCORO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

set(QVW_ROOT=${CMAKE_CURRENT_SOURCE_DIR})


include(FetchContent)

if(DEFINED FETCHCONTENT_SOURCE_DIR_whisper_cpp)
  message(STATUS "Using pre-fetched llama.cpp: ${FETCHCONTENT_SOURCE_DIR_whisper_cpp}")
endif()

if(DEFINED FETCHCONTENT_SOURCE_DIR_llama_cpp)
  message(STATUS "Using pre-fetched llama.cpp: ${FETCHCONTENT_SOURCE_DIR_llama_cpp}")
endif()

include(cmake/QvwFetch.cmake)

qvw_fetch(qcoro
  GIT_REPOSITORY https://github.com/qcoro/qcoro.git
  GIT_TAG        v0.12.0
)

qvw_fetch(logfault
  GIT_REPOSITORY https://github.com/jgaa/logfault.git
  GIT_TAG        v0.8.5
)


# ---------- Externally built libraries begin

include(ExternalProject)

set(QVW_DEPS_PREFIX "${CMAKE_BINARY_DIR}/_deps_install")
set(QVW_SHARED_LIB_DIR "${QVW_DEPS_PREFIX}/lib")
set(QVW_WHISPER_WRAP_LIB "${QVW_SHARED_LIB_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}qvw_whisper_wrap${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(QVW_LLAMA_WRAP_LIB "${QVW_SHARED_LIB_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}qvw_llama_wrap${CMAKE_SHARED_LIBRARY_SUFFIX}")

########### Whisper

ExternalProject_Add(qvw_whisper_wrap_ep
  SOURCE_DIR  "${CMAKE_SOURCE_DIR}/wrappers/whisper"

  CMAKE_ARGS
    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
    -DQVW_GPU_BACKEND:STRING=${QVW_GPU_BACKEND_EFFECTIVE}
    -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${QVW_SHARED_LIB_DIR}
    -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=<BINARY_DIR>/private-archives

  BUILD_COMMAND
    ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG>

  INSTALL_COMMAND ""

  BUILD_BYPRODUCTS
    ${QVW_WHISPER_WRAP_LIB}
)

# Create an imported target that your app links to
add_library(qvw::whisper_wrap SHARED IMPORTED GLOBAL)

set_target_properties(qvw::whisper_wrap PROPERTIES
  IMPORTED_LOCATION "${QVW_WHISPER_WRAP_LIB}"
  #INTERFACE_INCLUDE_DIRECTORIES "${QVW_DEPS_PREFIX}/include"
)

add_dependencies(qvw::whisper_wrap qvw_whisper_wrap_ep)

############### Llama

ExternalProject_Add(qvw_llama_wrap_ep
  SOURCE_DIR  "${CMAKE_SOURCE_DIR}/wrappers/llama"

  CMAKE_ARGS
    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
    -DQVW_GPU_BACKEND:STRING=${QVW_GPU_BACKEND_EFFECTIVE}
    -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${QVW_SHARED_LIB_DIR}
    -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=<BINARY_DIR>/private-archives

  BUILD_COMMAND
    ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG>

  INSTALL_COMMAND ""

  BUILD_BYPRODUCTS
    ${QVW_LLAMA_WRAP_LIB}
)

add_library(qvw::llama_wrap SHARED IMPORTED GLOBAL)
set_target_properties(qvw::llama_wrap PROPERTIES
  IMPORTED_LOCATION "${QVW_LLAMA_WRAP_LIB}"
)

add_dependencies(qvw::llama_wrap qvw_llama_wrap_ep)


# ---------- Externally built libraries end

find_package(Qt6 REQUIRED COMPONENTS Core Quick Qml Multimedia)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(${PROJECT_NAME}
    src/app/main.cpp
    src/app/logging.h
)


set(QML_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

qt_add_resources(${PROJECT_NAME} app_icons
    PREFIX "/"
    FILES icons/qvw.svg
)

qt_add_qml_module(${PROJECT_NAME}
    URI ${PROJECT_NAME}
    VERSION 1.0
      QML_FILES
          qml/AboutDlg.qml
          qml/ChatPane.qml
          qml/DownloadProgress.qml
          qml/Main.qml
          qml/ModelPicker.qml
          qml/RecordingLevel.qml
          qml/TranscribePane.qml
          qml/TranslationPane.qml
          qml/common.js
          qml/settings/GeneralSettings.qml
          qml/settings/SettingsDlg.qml
          qml/settings/TranscribeSettings.qml
          qml/MessagesView.qml
      SOURCES
          include/qvw/EngineBase.h
          include/qvw/LlamaEngine.h
          include/qvw/WhisperEngine.h
          include/qvw/log_wrapper.h
          src/app/AppEngine.cpp
          src/app/AppEngine.h
          src/app/AudioCaptureDevice.cpp
          src/app/AudioCaptureDevice.h
          src/app/AudioController.cpp
          src/app/AudioController.h
          src/app/AudioFileWriter.cpp
          src/app/AudioFileWriter.h
          src/app/AudioRecorder.cpp
          src/app/AudioRecorder.h
          src/app/AudioRingBuffer.cpp
          src/app/AudioRingBuffer.h
          src/app/AvailableModelsModel.cpp
          src/app/AvailableModelsModel.h
          src/app/ChatConversation.cpp
          src/app/ChatConversation.h
          src/app/ChatMessagesModel.cpp
          src/app/ChatMessagesModel.h
          src/app/GeneralModel.cpp
          src/app/GeneralModel.h
          src/app/LanguagesModel.cpp
          src/app/LanguagesModel.h
          src/app/Model.cpp
          src/app/Model.h
          src/app/ModelInfo.cpp
          src/app/ModelInfo.h
          src/app/ModelMgr.cpp
          src/app/ModelMgr.h
          src/app/ModelState.h
          src/app/Queue.h
          src/app/RewriteStyleModel.cpp
          src/app/RewriteStyleModel.h
          src/app/ScopedTimer.h
          src/app/Transcriber.cpp
          src/app/Transcriber.h
          src/app/TranscriberWhisper.cpp
          src/app/TranscriberWhisper.h
          src/app/AudioImport.h
          src/app/AudioImport.cpp
)

# Add includepaths
target_include_directories(${PROJECT_NAME} PRIVATE
   ${QML_INCLUDES}
   ${CMAKE_CURRENT_SOURCE_DIR}/src/app
   ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(${PROJECT_NAME} PRIVATE -frtti)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    QVW_GPU_BACKEND_AVAILABLE=${QVW_GPU_BACKEND_AVAILABLE}
)

add_dependencies(${PROJECT_NAME}
  qvw_whisper_wrap_ep
  qvw_llama_wrap_ep
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Qt6::Core
        Qt6::Quick
        Qt6::Qml
        Qt6::Multimedia
        logfault::logfault
        qvw::llama_wrap
        qvw::whisper_wrap
        QCoro6::Core
        QCoro6::Network
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
  "${QVW_WHISPER_WRAP_LIB}"
  "${QVW_LLAMA_WRAP_LIB}"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
