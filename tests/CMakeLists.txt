# tests/CMakeLists.txt — 领域层单元测试（不链 eui::neo）。
#
# 测试目标自带 main()，所以**不能**链 EUI-NEO 库、也不能把 glfw_app_main.cpp 放进来
# （那个 TU 会再提供一个 main）。这里只编领域模块：纯逻辑，不需要窗口和 GL。
# 形状对齐姊妹项目 llm-switch 的 test_store / test_skills。

# ---- ectave.utils：纯字符串 / UTF-8 工具 ----
add_executable(test_utils test_utils.cpp)
target_sources(test_utils PRIVATE FILE_SET ectave_modules TYPE CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.."
    FILES ../src/utils.cppm)
set_target_properties(test_utils PROPERTIES CXX_MODULE_STD ON CXX_SCAN_FOR_MODULES ON)
target_compile_features(test_utils PRIVATE cxx_std_23)
target_link_libraries(test_utils PRIVATE Threads::Threads)
add_test(NAME utils COMMAND test_utils)

# ---- ectave.store：行模型 / 历史 / 输出分类 ----
# store 导入 ectave.octave_engine，所以两个模块接口都要进 FILE_SET。只是 import 不会
# 启动 Octave —— 引擎进程只在显式调用 start()/send() 时才 spawn，测试不碰那些路径。
#
# 必须链 eui::neo：octave_engine 的读线程在收到引擎事件后调 EUI 的
# core::platform::requestUiUpdate()（定义在 core/platform/platform.cpp），那是 EUI 库
# 里的符号。链它不会带进 main()（glfw_app_main.cpp 只加到 ectave 目标上），也不会
# 需要显示器——只有真开窗口才需要。test_utils 不碰这条路径，所以不链。
add_executable(test_store test_store.cpp)
target_sources(test_store PRIVATE FILE_SET ectave_modules TYPE CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.."
    FILES ../src/store.cppm ../src/utils.cppm ../src/octave_engine.cppm)
set_target_properties(test_store PROPERTIES CXX_MODULE_STD ON CXX_SCAN_FOR_MODULES ON)
target_compile_features(test_store PRIVATE cxx_std_23)
target_link_libraries(test_store PRIVATE eui::neo Threads::Threads)
if(APPLE)
    # octave_engine.cppm 用 <util.h> 的 forkpty；Linux 上 forkpty 在 libc 里。
    target_link_libraries(test_store PRIVATE util)
endif()
add_test(NAME store COMMAND test_store)

# ---- ectave.octave_engine：真起 octave 子进程的端到端 ----
# 唯一能覆盖 PTY 读写链路（回显/提示符/哨兵/CRLF）的测试——假数据测不出这些，
# 而那正是出过 bug 的地方。无头可跑（requestUiUpdate 在 GLFW 未初始化时是 no-op），
# 但会真 spawn octave，所以给足超时。没有引擎的机器上自报退出码 77 → CTest SKIP。
add_executable(test_engine test_engine.cpp)
target_sources(test_engine PRIVATE FILE_SET ectave_modules TYPE CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.."
    FILES ../src/octave_engine.cppm ../src/utils.cppm)
set_target_properties(test_engine PROPERTIES CXX_MODULE_STD ON CXX_SCAN_FOR_MODULES ON)
target_compile_features(test_engine PRIVATE cxx_std_23)
target_link_libraries(test_engine PRIVATE eui::neo Threads::Threads)
if(APPLE)
    target_link_libraries(test_engine PRIVATE util)
endif()
add_test(NAME engine COMMAND test_engine)
set_tests_properties(engine PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
