cmake_minimum_required(VERSION 3.16)

project(QVocalWriter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(QCORO_WITH_QTQUICK OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QML     OFF CACHE BOOL "" FORCE)
set(QCORO_BUILD_TESTS OFF CACHE BOOL "" FORCE)


include(FetchContent)
FetchContent_Declare(
  logfault_src
  GIT_REPOSITORY https://github.com/jgaa/logfault.git
  #FIND_PACKAGE_ARGS NAMES logfault
  #GIT_TAG
)

# whisper.cpp
FetchContent_Declare(
  whisper_cpp
  GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
  GIT_TAG v1.8.2
)

# Configure whisper.cpp BEFORE MakeAvailable
# (these are CMake options defined by whisper.cpp itself)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)

# CPU-only, no CUDA/Metal/etc for now – keeps it simple & portable
set(GGML_CUDA       OFF CACHE BOOL "" FORCE)
set(GGML_HIPBLAS    OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN     OFF CACHE BOOL "" FORCE)
set(GGML_METAL      OFF CACHE BOOL "" FORCE)
set(GGML_OPENBLAS   OFF CACHE BOOL "" FORCE)
set(GGML_BLAS       OFF CACHE BOOL "" FORCE)

# QCoro (coroutines for Qt5/Qt6)
FetchContent_Declare(
  qcoro
  GIT_REPOSITORY https://github.com/qcoro/qcoro.git
  GIT_TAG v0.12.0
)

FetchContent_MakeAvailable(logfault_src whisper_cpp qcoro)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Qml Multimedia)
find_package(Qt6 REQUIRED COMPONENTS Core)

qt_standard_project_setup(REQUIRES 6.4)

qt_add_executable(${PROJECT_NAME}
    src/main.cpp
    src/logging.h
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
            qml/RecordingPane.qml
            qml/Main.qml
        SOURCES
            src/AppEngine.cpp
            src/AppEngine.h
            src/AudioCaptureDevice.cpp
            src/AudioCaptureDevice.h
            src/AudioController.cpp
            src/AudioController.h
            src/AudioFileWriter.cpp
            src/AudioFileWriter.h
            src/AudioRecorder.cpp
            src/AudioRecorder.h
            src/AudioRingBuffer.cpp
            src/AudioRingBuffer.h
            src/Model.cpp
            src/Model.h
            src/ModelInfo.h
            src/ModelMgr.cpp
            src/ModelMgr.h
            src/Queue.h
            src/ScopedTimer.h
            src/Transcriber.cpp
            src/Transcriber.h
            src/TranscriberWhisper.cpp
            src/TranscriberWhisper.h
            src/WhisperInstance.cpp
            src/WhisperInstance.h
            QML_FILES qml/RecordingLevel.qml
)

# Add includepaths
target_include_directories(${PROJECT_NAME} PRIVATE
   ${QML_INCLUDES}
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
