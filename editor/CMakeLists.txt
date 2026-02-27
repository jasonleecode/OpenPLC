cmake_minimum_required(VERSION 3.16)

# 项目名称：TiZi
# 版本：0.1.0
# 语言：C++
project(TiZi VERSION 0.1.0 LANGUAGES CXX)

# ==========================================
# 1. 基础配置
# ==========================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动处理 Qt 的 MOC (元对象编译器), UIC (UI文件), RCC (资源文件)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ==========================================
# 2. 查找 Qt6 组件
# ==========================================
# Widgets: 基础窗口控件
# Core: 核心非GUI功能
# Gui: 绘图基础
# Svg: 用于渲染矢量电气符号 (关键)
# Xml: 用于保存/加载 PLC 项目文件
# PrintSupport: 用于打印梯形图
find_package(Qt6 COMPONENTS
    Widgets
    Core
    Gui
    Svg
    Xml
    PrintSupport
    SerialPort
    Network
    REQUIRED
)

# ==========================================
# 3. 源文件管理
# ==========================================
# 这里我们显式列出文件，而不是用 GLOB，这样工程结构更清晰，改名更安全。
# 随着项目开发，你需要不断往这里添加新的 .cpp 和 .h 文件。

set(PROJECT_SOURCES
    # 入口
    src/main.cpp
    
    # App 层 (主窗口 + 项目管理)
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/app/ProjectManager.cpp
    src/app/ProjectManager.h

    # Editor 层 (我们先预留这些位置，后续会填入具体代码)
    src/editor/scene/LadderScene.cpp
    src/editor/scene/LadderScene.h
    src/editor/scene/LadderView.cpp
    src/editor/scene/LadderView.h
    src/editor/scene/PlcOpenViewer.h
    src/editor/scene/PlcOpenViewer.cpp
    src/editor/items/BaseItem.h
    src/editor/items/ContactItem.h
    src/editor/items/ContactItem.cpp
    src/editor/items/CoilItem.h
    src/editor/items/CoilItem.cpp
    src/editor/items/WireItem.h
    src/editor/items/WireItem.cpp
    src/core/compiler/CodeGenerator.h
    src/core/compiler/CodeGenerator.cpp
    src/core/compiler/StGenerator.h
    src/core/compiler/StGenerator.cpp

    # Editor 元件（新增）
    src/editor/items/FunctionBlockItem.h
    src/editor/items/FunctionBlockItem.cpp
    src/editor/items/VarBoxItem.h
    
    # Core 层 — 数据模型
    src/core/models/VariableDecl.h
    src/core/models/PouModel.h
    src/core/models/PouModel.cpp
    src/core/models/ProjectModel.h
    src/core/models/ProjectModel.cpp

    # 工具类
    src/utils/StHighlighter.h
    src/utils/UndoStack.h
    src/utils/UndoStack.cpp

    # 通信 / 下载
    src/comm/IPlcTransport.h
    src/comm/SerialTransport.h
    src/comm/SerialTransport.cpp
    src/comm/TcpTransport.h
    src/comm/TcpTransport.cpp
    src/comm/PlcProtocol.h
    src/comm/PlcProtocol.cpp
    src/comm/DownloadDialog.h
    src/comm/DownloadDialog.cpp

    # 资源文件
    resources/tizi.qrc
)

# ==========================================
# 4. 定义可执行文件
# ==========================================
# 在 Windows 上开启 WIN32 选项，避免运行时弹出黑色控制台窗口
if(WIN32)
    add_executable(TiZi WIN32 ${PROJECT_SOURCES})
else()
    add_executable(TiZi ${PROJECT_SOURCES})
endif()

# ==========================================
# 4b. 编译时宏：library.xml 路径（源码目录绝对路径，仅开发时使用）
# ==========================================
target_compile_definitions(TiZi PRIVATE
    LIBRARY_XML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/conf/library.xml"
    MATIEC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tools/matiec_mac"
    DRIVERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/drivers"
    WASI_SDK_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tools/wasm/wasi-sdk"
)

# ==========================================
# 5. 链接库
# ==========================================
target_link_libraries(TiZi PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Svg
    Qt6::Xml
    Qt6::PrintSupport
    Qt6::SerialPort
    Qt6::Network
)

# ==========================================
# 6. 编译选项优化
# ==========================================
if(MSVC)
    target_compile_options(TiZi PRIVATE /W3 /MP) # MSVC: 3级警告，多核编译
else()
    target_compile_options(TiZi PRIVATE -Wall -Wextra) # GCC/Clang: 开启大部分警告
endif()

# ==========================================
# 7. 安装部署 (可选)
# ==========================================
install(TARGETS TiZi
    BUNDLE DESTINATION .
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)