cmake_minimum_required(VERSION 3.21)

project(mcPacker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()
# ---------------------------------------------------------------------------
# 核心库：与界面解耦，可单独复用/测试
# ---------------------------------------------------------------------------
set(MCPACKER_CORE_SOURCES
    src/core/Common.h
    src/core/Common.cpp
    src/core/Crc32.h
    src/core/Crc32.cpp
    src/core/Deflate.h
    src/core/Deflate.cpp
    src/core/ZipWriter.h
    src/core/ZipWriter.cpp
    src/core/ZipReader.h
    src/core/ZipReader.cpp
    src/core/PclSettings.h
    src/core/PclSettings.cpp
    src/core/MinecraftLayout.h
    src/core/MinecraftLayout.cpp
    src/core/JavaScanner.h
    src/core/JavaScanner.cpp
    src/core/Manifest.h
    src/core/Manifest.cpp
    src/core/Packer.h
    src/core/Packer.cpp
    src/core/Unpacker.h
    src/core/Unpacker.cpp
)

add_library(mcpacker_core STATIC ${MCPACKER_CORE_SOURCES})
target_include_directories(mcpacker_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mcpacker_core PUBLIC Qt6::Core)
target_compile_definitions(mcpacker_core PUBLIC MCPACKER_VERSION="${PROJECT_VERSION}")

# Windows 下源码含中文时，显式固定输入/执行字符集，避免 MinGW 按 GBK 解析
if(MINGW)
    target_compile_options(mcpacker_core PUBLIC -finput-charset=UTF-8 -fexec-charset=UTF-8)
    target_compile_options(mcpacker_core PUBLIC -Wall -Wextra -Wno-unused-parameter)
elseif(MSVC)
    target_compile_options(mcpacker_core PUBLIC /utf-8)
endif()

# ---------------------------------------------------------------------------
# 图形界面程序
# ---------------------------------------------------------------------------
set(MCPACKER_APP_SOURCES
    src/main.cpp
    src/app.rc
    src/resources.qrc
    src/ui/LogPanel.h
    src/ui/LogPanel.cpp
    src/ui/PackPage.h
    src/ui/PackPage.cpp
    src/ui/UnpackPage.h
    src/ui/UnpackPage.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

add_executable(mcPacker WIN32 ${MCPACKER_APP_SOURCES})
target_link_libraries(mcPacker PRIVATE mcpacker_core Qt6::Widgets)

# 图标 / 版本资源：windres 需要能找到 app.ico
if(MINGW)
    set_source_files_properties(src/app.rc PROPERTIES
        COMPILE_OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

set_target_properties(mcPacker PROPERTIES
    OUTPUT_NAME "mcPacker"
    WIN32_EXECUTABLE TRUE
)

# ---------------------------------------------------------------------------
# 部署：windeployqt
# ---------------------------------------------------------------------------
get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
find_program(WINDEPLOYQT_EXE windeployqt HINTS "${QT_BIN_DIR}" NO_DEFAULT_PATH)

if(WINDEPLOYQT_EXE)
    set(DEPLOY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist")
    add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:mcPacker>" "${DEPLOY_DIR}/"
        COMMAND "${WINDEPLOYQT_EXE}" --no-translations --no-opengl-sw --no-system-d3d-compiler
                "${DEPLOY_DIR}/mcPacker.exe"
        COMMENT "部署 mcPacker 到 ${DEPLOY_DIR}（含 Qt 运行库）"
        DEPENDS mcPacker
    )
endif()

install(TARGETS mcPacker RUNTIME DESTINATION .)

# ---------------------------------------------------------------------------
# 命令行工具（控制台程序）：脚本化打包/解包 + 自动化自测
# ---------------------------------------------------------------------------
add_executable(mcPackerCli tools/mcPackerCli.cpp)
target_link_libraries(mcPackerCli PRIVATE mcpacker_core)
set_target_properties(mcPackerCli PROPERTIES OUTPUT_NAME "mcPackerCli")
install(TARGETS mcPackerCli RUNTIME DESTINATION .)
