cmake_minimum_required(VERSION 3.16)

project(QVocalWriter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(QCORO_WITH_QTQUICK OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QML     OFF CACHE BOOL "" FORCE)
set(QCORO_BUILD_TESTS OFF CACHE BOOL "" FORCE)

set(QVW_ROOT=${CMAKE_CURRENT_SOURCE_DIR})


include(FetchContent)
FetchContent_Declare(
  logfault_src
  GIT_REPOSITORY https://github.com/jgaa/logfault.git
  #FIND_PACKAGE_ARGS NAMES logfault
  #GIT_TAG
)

# # llama.cpp
# FetchContent_Declare(
#   llama_cpp
#   GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
#   # Prefer pinning to a release tag or commit for reproducible builds:
#   GIT_TAG master
# )

# CPU-only: disable all GPU/backends for now
set(GGML_CUDA     OFF CACHE BOOL "" FORCE)
set(GGML_HIPBLAS  OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN   OFF CACHE BOOL "" FORCE)
set(GGML_METAL    OFF CACHE BOOL "" FORCE)
set(GGML_SYCL     OFF CACHE BOOL "" FORCE)
set(GGML_OPENCL   OFF CACHE BOOL "" FORCE)
set(GGML_RPC      OFF CACHE BOOL "" FORCE)
set(GGML_KOMPUTE  OFF CACHE BOOL "" FORCE)
set(GGML_CANN     OFF CACHE BOOL "" FORCE)
set(GGML_MUSA     OFF CACHE BOOL "" FORCE)

# Also keep BLAS off for now (simple/portable CPU baseline)
set(GGML_BLAS     OFF CACHE BOOL "" FORCE)
set(GGML_OPENBLAS OFF CACHE BOOL "" FORCE)


# # whisper.cpp
# FetchContent_Declare(
#   whisper_cpp
#   GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
#   GIT_TAG master
# )

# Configure whisper.cpp BEFORE MakeAvailable
# (these are CMake options defined by whisper.cpp itself)
# set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
# set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)

# QCoro (coroutines for Qt5/Qt6)
FetchContent_Declare(
  qcoro
  GIT_REPOSITORY https://github.com/qcoro/qcoro.git
  GIT_TAG v0.12.0
)

#FetchContent_MakeAvailable(logfault_src llama_cpp whisper_cpp qcoro)
# FetchContent_MakeAvailable(whisper_cpp)
# FetchContent_MakeAvailable(llama_cpp)
FetchContent_MakeAvailable(logfault_src qcoro)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Qml Multimedia)
find_package(Qt6 REQUIRED COMPONENTS Core)

qt_standard_project_setup(REQUIRES 6.4)

qt_add_executable(${PROJECT_NAME}
    src/app/main.cpp
    src/app/logging.h
)


target_link_libraries(QVocalWriter PRIVATE Qt6::Core)
set(QML_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

qt_add_qml_module(${PROJECT_NAME}
    URI ${PROJECT_NAME}
    VERSION 1.0
    QML_FILES
        QML_FILES
            qml/RecordingLevel.qml
            qml/RecordingPane.qml
            qml/Main.qml
        SOURCES
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
            src/app/Model.cpp
            src/app/Model.h
            src/app/ModelInfo.h
            src/app/ModelMgr.cpp
            src/app/ModelMgr.h
            src/app/Queue.h
            src/app/ScopedTimer.h
            src/app/Transcriber.cpp
            src/app/Transcriber.h
            src/app/TranscriberWhisper.cpp
            src/app/TranscriberWhisper.h
            src/app/WhisperInstance.cpp
            src/app/WhisperInstance.h
            include/qvw/EngineBase.h
            include/qvw/WhisperEngine.h
)

# Add includepaths
target_include_directories(${PROJECT_NAME} PRIVATE
   ${QML_INCLUDES}
   ${QVW_ROOT}/include
)


target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Qt6::Core
        Qt6::Quick
        Qt6::Qml
        Qt6::Multimedia
        logfault::logfault
        #whisper_cpp::whisper_cpp
        whisper
        llama
        QCoro6::Core
        QCoro6::Network
)

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
