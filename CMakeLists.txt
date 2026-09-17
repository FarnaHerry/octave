# CMakeLists.txt — ectave 顶层构建。
#
# 形状对齐同目录姊妹项目（llm-switch / Clash-Flux / apitab）的 C++23 模块脚手架：
# CxxImportStdGate 门控、src/ 递归源码发现、模块接口 .cppm 显式清单走 FILE_SET、
# tests/ 挂 CTest、CPack 出 Linux 安装包。差异只在 UI 依赖：它们用 HuxerUI（CLI 生成
# 格式），ectave 用 EUI-NEO，且直接消费上游源码而非预编译 SDK（见 cmake/ectave_vendor.cmake）。
#
# 常用命令：
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --parallel
#   ctest --test-dir build --output-on-failure
#   ./run.sh                                  # 起 GUI（会 cd 到 build/）
#   cpack --config build/CPackConfig.cmake -G DEB -B dist
#   或直接用预设：cmake --preset ninja-release && cmake --build --preset ninja-release
cmake_minimum_required(VERSION 3.30)

project(ectave VERSION 0.2.0
    DESCRIPTION "EUI-NEO 套壳 GNU Octave 的桌面工作台"
    LANGUAGES NONE)

# `import std` 的 experimental 门控必须在 enable_language(CXX) 之前设置
# （UUID 随 CMake 版本变，见 cmake/CxxImportStdGate.cmake 顶部注释）。
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CxxImportStdGate)
enable_language(C)
enable_language(CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)            # gnu++23：模块实现与 GCC 标准库组合最顺
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# 只给用 import std 的目标开 CXX_MODULE_STD（ectave / 测试目标），
# 不影响 EUI-NEO 这个 C++17 子项目。
set(CMAKE_CXX_MODULE_STD OFF)

# Windows 用静态 CRT：发行包不必再带 vcruntime140*.dll（旧 mcpp 版靠拷 DLL 解决）。
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

# ---- EUI-NEO（上游源码，四级优先选择）---------------------------------------
# 子目录模式下 EUI 自己的 CMakeLists 会把 CMAKE_CXX_STANDARD 设成 17 —— 那是它自己
# 目录作用域的事，本目录（以及下面的 ectave 目标）仍是 C++23。
include(cmake/ectave_vendor.cmake)
add_subdirectory("${ECTAVE_EUI_SOURCE_DIR}" eui-neo)

# ---- ectave 目标 -------------------------------------------------------------
# src/ 下递归 glob 普通 .cpp（当前只有 app.cpp：dslAppConfig + compose）；模块接口
# .cppm 不经 glob，由下面的 FILE_SET 显式追加。
file(GLOB_RECURSE ECTAVE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

set(ECTAVE_MODULE_UNITS
    src/utils.cppm
    src/store.cppm
    src/octave_engine.cppm
    src/ui/theme.cppm
    src/ui/utils.cppm
    src/ui/command_input.cppm
    src/ui/console_page.cppm
    src/ui/script_page.cppm
    src/ui/workspace_page.cppm
)

# main() 由 EUI 的 app 入口 TU 提供 —— 这是旧 mcpp 配置里 `app-main` feature 的等价物。
# 刻意**不用** EUI 的 eui_neo_configure_app()：那个便捷函数会顺带调
# eui_apply_compile_options()，给目标加上 -Os -fno-exceptions -fno-rtti，和本项目满屏的
# `import std;` 直接冲突（std 模块是按开异常/RTTI 构建的）。所以入口源、资源拷贝、
# 平台链接选项都在这里手动接线。
add_executable(ectave
    ${ECTAVE_SOURCES}
    "${ECTAVE_EUI_SOURCE_DIR}/core/app/glfw_app_main.cpp")

target_sources(ectave PRIVATE FILE_SET ectave_modules TYPE CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${ECTAVE_MODULE_UNITS})
set_target_properties(ectave PROPERTIES CXX_MODULE_STD ON)
# 显式打开全源扫描：模块实现单元（普通 .cpp 里的 module ectave.*;）必须经扫描才能
# 拿到 -fmodules-ts。
set_target_properties(ectave PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_features(ectave PRIVATE cxx_std_23)
# eui_ui.h 是 UI 模块 global module fragment 的入口，按引号包含（src/ui/ 下）。
target_include_directories(ectave PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
target_link_libraries(ectave PRIVATE eui::neo Threads::Threads)

# 资源：EUI 自带的 assets（默认字体/图标/着色器）先铺，本项目的字体后铺 —— 同名以本项目
# 为准。EUI 的字体解析（core/render/text.cpp resolveFontFilePath）会把
# <可执行文件目录>/assets/<文件名> 作为第二优先候选，所以字体放在 exe 旁边就行，
# 不依赖启动时的 cwd（deb/rpm 安装后也成立）。
add_custom_command(TARGET ectave POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ectave>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ECTAVE_EUI_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ectave>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ectave>/assets"
    COMMENT "ectave: 铺设 assets/（EUI 默认资源 + 本项目字体）"
    VERBATIM)

if(WIN32)
    # 对应旧 mcpp 配置里的 -Wl,-subsystem:windows -Wl,-entry:mainCRTStartup：
    # GUI 子系统（不起控制台窗口）而以 main() 为入口。_WIN32_WINNT 是给 MinGW 用的
    # （MSVC 的 sdkddkver.h 本来就默认 0x0A00，写上等价）。
    target_compile_definitions(ectave PRIVATE _WIN32_WINNT=0x0A00)
    target_link_options(ectave PRIVATE /ENTRY:mainCRTStartup /SUBSYSTEM:WINDOWS)
elseif(APPLE)
    # octave_engine.cppm 用 <util.h> 的 forkpty（glibc 在 <pty.h>，macOS 在 libutil）。
    target_link_libraries(ectave PRIVATE util)
endif()

# ---- 测试 --------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# ---- 安装 / 打包（Linux 安装包；发行压缩包由 packaging/ 脚本另做）------------
if(UNIX AND NOT APPLE)
    # 装到 lib/ectave/ 而不是 bin/：EUI 按 <exeDir>/assets 找字体，二进制必须和 assets
    # 同一个目录。bin/ 下只放一个指向它的符号链接，/proc/self/exe 解析的是真实路径，
    # 所以字体照样找得到。
    install(TARGETS ectave RUNTIME DESTINATION lib/ectave)
    # 顺序要紧：EUI 的默认资源（着色器、默认字体、图标）先装，本项目字体后装覆盖同名
    # 文件——与上面 POST_BUILD 的拷贝顺序一致。漏掉 EUI 那一层，装出来的包会缺
    # shaders/ 而渲染不出东西。
    install(DIRECTORY "${ECTAVE_EUI_SOURCE_DIR}/assets/"
        DESTINATION lib/ectave/assets)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
        DESTINATION lib/ectave/assets)
    # 桌面入口：菜单里能找到。图标用 EUI 自带的 icon.png（360×351，非标准尺寸，
    # 所以放尺寸无关的 share/pixmaps 而不是谎报进 hicolor/256x256）。
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/ectave.desktop"
        DESTINATION share/applications)
    install(FILES "${ECTAVE_EUI_SOURCE_DIR}/assets/icon.png"
        DESTINATION share/pixmaps
        RENAME ectave.png)
    install(CODE "
        execute_process(COMMAND \${CMAKE_COMMAND} -E make_directory
            \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\")
        execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
            ../lib/ectave/ectave \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/ectave\")")

    set(CPACK_PACKAGE_NAME "ectave")
    set(CPACK_PACKAGE_VENDOR "FarnaHerry")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "EUI-NEO 套壳 GNU Octave 的桌面工作台（需要系统 Octave 或在程序旁放内置引擎）")
    set(CPACK_PACKAGE_CONTACT "FarnaHerry")
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    include(CPack)
endif()
