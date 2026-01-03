cmake_minimum_required(VERSION 3.16)


project(QVocalWriter VERSION 0.1.0 LANGUAGES CXX)

add_definitions(-DAPP_VERSION=\"${CMAKE_PROJECT_VERSION}\")

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

# QCoro (coroutines for Qt5/Qt6)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  qcoro
  GIT_REPOSITORY https://github.com/qcoro/qcoro.git
  GIT_TAG v0.12.0
)

FetchContent_MakeAvailable(logfault_src qcoro)

# ---------- Externally built libraries begin

include(ExternalProject)

set(QVW_DEPS_PREFIX "${CMAKE_BINARY_DIR}/_deps_install")

########### Whisper

ExternalProject_Add(qvw_whisper_wrap_ep
  SOURCE_DIR  "${CMAKE_SOURCE_DIR}/wrappers/whisper"

  CMAKE_ARGS
    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
    -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${QVW_DEPS_PREFIX}/lib
    -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=<BINARY_DIR>/private-archives

  BUILD_COMMAND
    ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG>

  INSTALL_COMMAND ""

  BUILD_BYPRODUCTS
    ${QVW_DEPS_PREFIX}/lib/libqvw_whisper_wrap.so
)

# Create an imported target that your app links to
add_library(qvw::whisper_wrap SHARED IMPORTED GLOBAL)

set_target_properties(qvw::whisper_wrap PROPERTIES
  IMPORTED_LOCATION "${QVW_DEPS_PREFIX}/lib/libqvw_whisper_wrap.so"
  #INTERFACE_INCLUDE_DIRECTORIES "${QVW_DEPS_PREFIX}/include"
)

add_dependencies(qvw::whisper_wrap qvw_whisper_wrap_ep)

############### Llama

set(QVW_DEPS_PREFIX "${CMAKE_BINARY_DIR}/_deps_install")

ExternalProject_Add(qvw_llama_wrap_ep
  SOURCE_DIR  "${CMAKE_SOURCE_DIR}/wrappers/llama"

  CMAKE_ARGS
    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
    -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${QVW_DEPS_PREFIX}/lib
    -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=<BINARY_DIR>/private-archives

  BUILD_COMMAND
    ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG>

  INSTALL_COMMAND ""

  BUILD_BYPRODUCTS
    ${QVW_DEPS_PREFIX}/lib/libqvw_llama_wrap.so
)

add_library(qvw::llama_wrap SHARED IMPORTED GLOBAL)
set_target_properties(qvw::llama_wrap PROPERTIES
  IMPORTED_LOCATION "${QVW_DEPS_PREFIX}/lib/libqvw_llama_wrap.so"
)

add_dependencies(qvw::llama_wrap qvw_llama_wrap_ep)


# ---------- Externally built libraries end

find_package(Qt6 REQUIRED COMPONENTS Core Quick Qml Multimedia)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Qt6 REQUIRED COMPONENTS Core Quick)
find_package(Qt6 REQUIRED COMPONENTS Core)

qt_standard_project_setup(REQUIRES 6.4)

qt_add_executable(${PROJECT_NAME}
    src/app/main.cpp
    src/app/logging.h
)


target_link_libraries(QVocalWriter PRIVATE Qt6::Core)
target_link_libraries(QVocalWriter PRIVATE Qt6::Core)
target_link_libraries(QVocalWriter PRIVATE Qt6::Core)
target_link_libraries(QVocalWriter PRIVATE Qt6::Core Qt6::Quick)
target_link_libraries(QVocalWriter PRIVATE Qt6::Core)
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

add_dependencies(${PROJECT_NAME} qvw_whisper_wrap_ep)

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

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
