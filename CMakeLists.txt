cmake_minimum_required(VERSION 3.16)

project(QVocalWriter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Quick Qml)

qt_standard_project_setup(REQUIRES 6.4)

qt_add_executable(qvocalwriter
    src/main.cpp
)

qt_add_qml_module(qvocalwriter
    URI QVocalWriter
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_link_libraries(qvocalwriter
    PRIVATE
        Qt6::Quick
        Qt6::Qml
)

set_target_properties(qvocalwriter PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
