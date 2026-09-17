# ectave_vendor.cmake — EUI-NEO 依赖选择。
#
# ectave 不再经 mcpp 包索引拿预编译的 compat.eui-neo，而是**直接消费 EUI 上游源码**
# （见 README「依赖」一节）：这样能用上 EUI 的新提交，也能在本机改 EUI 源码立刻重编。
#
# 选择优先级（固定顺序，与姊妹项目 llm-switch 的 HUXERUI_HOME 契约同形）：
#   1. EUI_NEO_HOME（环境变量或 -D）——显式指向任意 checkout，包括本机别处的仓库。
#   2. third_party/eui-neo            ——本仓库内 clone 的上游（默认命中；已 gitignore，
#                                        不入库，本地 `git -C third_party/eui-neo pull` 追新）。
#   3. ../EUI-NEO                     ——同目录的兄弟检出，只为本机开发便利存在（CI 没有）。
#   4. FetchContent GIT_TAG dev       ——兜底，需要网络。
# 四条都落空则 FATAL_ERROR 并打印克隆指引。
#
# 判定条件统一为「有 CMakeLists.txt + include/eui_neo.h」——只看目录存在会误判空目录。
#
# 结果变量：ECTAVE_EUI_SOURCE_DIR（根 CMakeLists 用它取 app 入口 core/app/glfw_app_main.cpp）。

set(EUI_NEO_HOME "$ENV{EUI_NEO_HOME}" CACHE PATH
    "EUI-NEO source checkout（优先于 third_party/eui-neo）")
set(ECTAVE_EUI_BUNDLED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eui-neo" CACHE PATH
    "仓库内 EUI-NEO checkout（gitignore，不入库）")
set(ECTAVE_EUI_SIBLING_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../EUI-NEO" CACHE PATH
    "同目录兄弟检出（仅本机便利）")

function(ectave_looks_like_eui_source dir result)
    if(dir AND EXISTS "${dir}/CMakeLists.txt" AND EXISTS "${dir}/include/eui_neo.h")
        set(${result} TRUE PARENT_SCOPE)
    else()
        set(${result} FALSE PARENT_SCOPE)
    endif()
endfunction()

set(ECTAVE_EUI_SOURCE_DIR "")

ectave_looks_like_eui_source("${EUI_NEO_HOME}" _ectave_ok)
if(_ectave_ok)
    set(ECTAVE_EUI_SOURCE_DIR "${EUI_NEO_HOME}")
    message(STATUS "ectave: EUI-NEO ← EUI_NEO_HOME (${ECTAVE_EUI_SOURCE_DIR})")
endif()

if(NOT ECTAVE_EUI_SOURCE_DIR)
    ectave_looks_like_eui_source("${ECTAVE_EUI_BUNDLED_DIR}" _ectave_ok)
    if(_ectave_ok)
        set(ECTAVE_EUI_SOURCE_DIR "${ECTAVE_EUI_BUNDLED_DIR}")
        message(STATUS "ectave: EUI-NEO ← third_party/eui-neo (${ECTAVE_EUI_SOURCE_DIR})")
    endif()
endif()

if(NOT ECTAVE_EUI_SOURCE_DIR)
    ectave_looks_like_eui_source("${ECTAVE_EUI_SIBLING_DIR}" _ectave_ok)
    if(_ectave_ok)
        set(ECTAVE_EUI_SOURCE_DIR "${ECTAVE_EUI_SIBLING_DIR}")
        message(STATUS "ectave: EUI-NEO ← 兄弟检出 ${ECTAVE_EUI_SOURCE_DIR}"
            "（本机便利通道；CI 上不存在，请确保 third_party/eui-neo 已 clone）")
    endif()
endif()

if(NOT ECTAVE_EUI_SOURCE_DIR)
    message(STATUS "ectave: 本地没有 EUI-NEO 源码，回落到 FetchContent（需要网络）")
    include(FetchContent)
    FetchContent_Declare(eui_neo
        GIT_REPOSITORY https://github.com/sudoevolve/EUI-NEO.git
        GIT_TAG dev
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(eui_neo)
    set(ECTAVE_EUI_SOURCE_DIR "${eui_neo_SOURCE_DIR}")
endif()

if(NOT ECTAVE_EUI_SOURCE_DIR)
    message(FATAL_ERROR
        "ectave: 找不到 EUI-NEO 源码。任选一种：\n"
        "  git clone https://github.com/sudoevolve/EUI-NEO.git third_party/eui-neo\n"
        "  git -C third_party/eui-neo checkout dev\n"
        "或 -DEUI_NEO_HOME=<已 clone 的 EUI-NEO 目录>")
endif()
