# 桌面客户端 — 实施架构与测试

## 1. 文档说明

### 1.1 文档目的

本文档冻结桌面客户端进入可执行实现阶段后的目录结构、构建开关、目标命名、C++/QML 分层边界、管理器与模型职责，以及测试目标布局。自本文档生效后，后续桌面端代码必须落在 `clients/desktop/` 约定结构内，不再接受“先写页面再补架构”的临时实现。

### 1.2 权威来源

| 来源 | 路径 | 用途 |
|------|------|------|
| 范围与成功标准 | `docs/desktop/00-scope-and-success.md` | Owner / Visitor 双产品流与 Linux-first / Windows-ready 边界 |
| 领域模型与错误分类 | `docs/desktop/02-domain-models-and-errors.md` | `DriveItem`、`OwnerSession`、`ShareVisitorSession`、`BatchActionResult` 规范模型 |
| 认证、网络与传输状态机 | `docs/desktop/03-auth-network-and-transfers.md` | `NetworkClient`、`RequestFactory`、`SessionStore`、`TransferStore` 的职责基线 |
| 导航模型与页面状态 | `docs/desktop/04-navigation-and-ui-state.md` | 页面清单、Shell 分离、页面状态与路由守卫 |
| 根构建文件 | `CMakeLists.txt`、`CMakePresets.json` | 现有后端目标、预设命名、可选桌面集成入口 |
| 后端测试构建 | `test/CMakeLists.txt` | 现有 `disk-test` 与 CTest 注册方式的兼容约束 |

### 1.3 冻结不变量

1. 桌面端实现目录固定为 `clients/desktop/`，不向现有 `src/`、`test/` 根目录混入 Qt 代码。
2. `DISK_ENABLE_DESKTOP` 是唯一桌面构建开关，默认关闭。
3. C++ 持有所有状态、网络、令牌与业务逻辑；QML 只负责界面与声明式绑定。
4. 可执行桌面开发默认采用 TDD；文档任务只做文档校验；每个已完成实现波次后执行完整回归。

---

## 2. 构建集成冻结

### 2.1 根构建开关

| 项目 | 冻结要求 |
|------|----------|
| 选项名 | `DISK_ENABLE_DESKTOP` |
| 默认值 | `OFF` |
| 开关性质 | **desktop build optional** |
| 默认策略 | **OFF by default** |
| 关闭时行为 | 根 `CMakeLists.txt` 不引入 `clients/desktop/`；`disk` 与 `disk-test` 保持现状；**backend-only builds continue to work**；无 Qt 依赖 |
| 开启时行为 | 根 `CMakeLists.txt` 通过 `if(DISK_ENABLE_DESKTOP)` 包裹 `add_subdirectory(clients/desktop)`，再注册桌面目标与桌面测试 |

### 2.2 Qt 模块清单

当 `DISK_ENABLE_DESKTOP=ON` 时，桌面子工程统一查找以下 Qt 6 模块，不额外扩大冻结基线：

> 冻结的 CMake 入口形式为：`find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Test)`。

| 模块 | 用途 |
|------|------|
| `Core` | `QObject`、`QAbstractListModel`、`QDateTime`、应用生命周期 |
| `Gui` | 桌面应用入口、窗口系统集成 |
| `Quick` | QML 场景、`StackView`、视图绑定 |
| `QuickControls2` | 桌面控件、表单、导航外壳 |
| `Network` | `QNetworkAccessManager`、请求/响应封装 |
| `Test` | `Qt Test`、`Qt Quick Test`、CTest 集成 |

### 2.3 冻结 target name

以下 target name 为后续实现的唯一命名基线：

| target name | 类型 | 说明 |
|------|------|------|
| `desktop-client` | 可执行程序 | 桌面客户端主程序 |
| `desktop-unit-tests` | 测试可执行程序 | C++ 侧 `Qt Test` 聚合入口，覆盖 managers / models / mapper / session 逻辑 |
| `desktop-quick-tests` | 测试可执行程序 | `Qt Quick Test` 聚合入口，覆盖 Shell、页面导航、页面状态与可见性行为 |

### 2.4 与现有后端构建的兼容规则

| 场景 | 冻结规则 |
|------|----------|
| 后端默认开发 | 继续使用现有 `linux-debug-clang`、`linux-release-clang`、`windows-*-clang-cl` 预设，不要求安装 Qt |
| 桌面端启用 | 在同一预设上追加 `-DDISK_ENABLE_DESKTOP=ON`；不改动现有 preset 名称 |
| 后端目标 | `disk`、`disk-test` 的源文件、链接项、测试注册方式保持不变 |
| 桌面目标 | 仅在 `clients/desktop/CMakeLists.txt` 与其子目录内声明，不影响后端目标图 |

---

## 3. `clients/desktop/` 目录布局

### 3.1 冻结目录树

```text
clients/desktop/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── network/
│   │   ├── NetworkClient.hpp
│   │   ├── NetworkClient.cpp
│   │   ├── RequestFactory.hpp
│   │   ├── RequestFactory.cpp
│   │   ├── ErrorAdapter.hpp
│   │   └── ErrorAdapter.cpp
│   ├── auth/
│   │   ├── AuthService.hpp
│   │   ├── AuthService.cpp
│   │   ├── SessionStore.hpp
│   │   ├── SessionStore.cpp
│   │   ├── OwnerSessionManager.hpp
│   │   ├── OwnerSessionManager.cpp
│   │   ├── VisitorSessionManager.hpp
│   │   └── VisitorSessionManager.cpp
│   ├── models/
│   │   ├── DriveItem.hpp
│   │   ├── DriveItem.cpp
│   │   ├── DriveListModel.hpp
│   │   ├── DriveListModel.cpp
│   │   ├── FolderTreeModel.hpp
│   │   ├── FolderTreeModel.cpp
│   │   ├── UploadTaskModel.hpp
│   │   ├── UploadTaskModel.cpp
│   │   ├── DownloadTaskModel.hpp
│   │   ├── DownloadTaskModel.cpp
│   │   ├── ShareListModel.hpp
│   │   ├── ShareListModel.cpp
│   │   ├── TrashListModel.hpp
│   │   ├── TrashListModel.cpp
│   │   ├── BatchResultModel.hpp
│   │   └── BatchResultModel.cpp
│   ├── managers/
│   │   ├── DriveManager.hpp
│   │   ├── DriveManager.cpp
│   │   ├── TransferManager.hpp
│   │   ├── TransferManager.cpp
│   │   ├── ShareManager.hpp
│   │   ├── ShareManager.cpp
│   │   ├── TrashManager.hpp
│   │   ├── TrashManager.cpp
│   │   ├── ProfileManager.hpp
│   │   └── ProfileManager.cpp
│   └── app/
│       ├── Application.hpp
│       ├── Application.cpp
│       ├── ShellController.hpp
│       └── ShellController.cpp
├── qml/
│   ├── Main.qml
│   ├── pages/
│   │   ├── SplashPage.qml
│   │   ├── LoginPage.qml
│   │   ├── DriveBrowserPage.qml
│   │   ├── TransferCenterPage.qml
│   │   ├── ShareManagementPage.qml
│   │   ├── TrashPage.qml
│   │   ├── SettingsPage.qml
│   │   ├── ShareVerifyPage.qml
│   │   └── ShareBrowsePage.qml
│   ├── components/
│   │   ├── PageStateView.qml
│   │   ├── BreadcrumbBar.qml
│   │   └── FolderTreePanel.qml
│   └── shells/
│       ├── OwnerShell.qml
│       └── VisitorShell.qml
└── tests/
    ├── CMakeLists.txt
    ├── fixtures/
    │   └── json/
    │       ├── auth/
    │       ├── models/
    │       ├── navigation/
    │       └── transfers/
    ├── helpers/
    │   ├── MockNetworkAccessManager.hpp
    │   ├── MockReplyFactory.hpp
    │   ├── TestJsonLoader.hpp
    │   └── QuickTestBootstrap.cpp
    ├── unit/
    │   ├── auth/
    │   ├── managers/
    │   ├── models/
    │   ├── navigation/
    │   └── transfers/
    └── quick/
        ├── navigation/
        ├── pages/
        ├── shells/
        └── transfers/
```

> **实现与文档同步说明**：`ProfilePage.qml` 的功能已合并到 `SettingsPage.qml` 中实现（O-26 ~ O-29 均在设置页完成）；`BatchResultPanel.qml` 和 `TransferListPanel.qml` 作为内联组件集成在各页面中，未独立为文件。`ErrorAdapter.hpp/.cpp` 是网络层的错误归一化组件，之前遗漏在目录树中。`DriveItem.cpp` 包含 `FromJson` / `ToJson` 的实现。

### 3.2 目录职责

| 路径 | 职责 |
|------|------|
| `clients/desktop/src/network/` | API 客户端、认证域请求工厂、统一错误归一化 |
| `clients/desktop/src/auth/` | 所有者与访客会话状态机、登录/刷新/分享访问入口 |
| `clients/desktop/src/models/` | 供 QML 绑定的 `QAbstractListModel` / `QAbstractItemModel` 实现与值对象 |
| `clients/desktop/src/managers/` | 按业务域聚合操作入口，驱动模型与网络层 |
| `clients/desktop/src/app/` | 应用单例、Shell 切换、上下文注入、启动恢复逻辑 |
| `clients/desktop/qml/pages/` | 页面级组件，只描述 UI 结构与状态绑定 |
| `clients/desktop/qml/components/` | 可复用控件、列表片段、页面状态片段 |
| `clients/desktop/qml/shells/` | `OwnerShell` / `VisitorShell` 顶层容器 |
| `clients/desktop/tests/unit/` | `Qt Test` 单元测试，验证 managers/models/navigation 逻辑 |
| `clients/desktop/tests/quick/` | `Qt Quick Test` UI 行为测试，验证页面与导航 |

---

## 4. C++ / QML 边界冻结

### 4.1 分层责任

| 层 | 负责内容 | 禁止内容 |
|------|----------|----------|
| C++ | 所有状态、所有网络调用、所有 token 管理、所有业务逻辑、错误归一化、路由守卫、任务调度 | 把原始 `QJsonObject` 或未归一化 JSON 直接暴露给 QML |
| QML | 布局、动画、视觉状态绑定、声明式 UI、组件组合、用户意图信号 | 业务逻辑、请求拼装、token 持久化、错误码决策 |

### 4.2 跨边界通信方式

| 方式 | 用途 |
|------|------|
| `Q_PROPERTY` | 暴露当前 Shell、页面状态、用户资料摘要、统计信息 |
| signals / slots | 表达用户意图（如登录、刷新列表、开始上传、关闭分享）与异步结果 |
| `QAbstractListModel` roles | 列表型数据绑定：文件列表、上传任务、下载任务、分享列表、回收站、批量结果 |
| `QAbstractItemModel` | 目录树模型，供树形导航或层级浏览 |

### 4.3 强制规则

1. QML 文件中 **NO business logic in QML files**；条件分支只允许用于显示切换，不允许承载业务决策。
2. QML 层 **NO raw JSON or QJsonObject exposed to QML**；一切后端负载必须先落入 C++ 规范模型。
3. 认证域头部注入只允许出现在 `RequestFactory`，不允许在页面或 delegate 中手工拼接。
4. `ShellController` 只根据 `SessionStore` 状态决定 `OwnerShell` / `VisitorShell` / `SplashPage` / `LoginPage` 的切换，QML 不直接推导认证状态机。

### 4.4 应用骨架

| 组件 | 冻结职责 |
|------|----------|
| `Application` | 进程级单例；构造 `NetworkClient`、`RequestFactory`、`AuthService`、`SessionStore`、各业务 Manager；向 QML 注册上下文对象 |
| `ShellController` | 顶层 Shell 选择、页面栈守卫、启动恢复、登录后默认路由、访客关闭流程 |
| `SessionStore` | Owner / Visitor 双状态机、single-flight refresh、令牌生命周期、会话广播 |

---

## 5. 管理器与模型冻结

### 5.1 Manager 类清单

以下类必须是 `QObject` 单例，或由 `Application` 作为唯一 owner 持有并向 QML 暴露：

| 类名 | 所在目录 | 冻结职责 |
|------|----------|----------|
| `AuthService` | `clients/desktop/src/auth/` | 登录、注册、刷新、登出、分享访问请求；仅负责认证 API 协议，不直接驱动页面 |
| `SessionStore` | `clients/desktop/src/auth/` | 持有 Owner + Visitor 会话状态机；统一处理 token 轮换、single-flight refresh、会话恢复与清理 |
| `DriveManager` | `clients/desktop/src/managers/` | 文件浏览、搜索、创建文件夹、重命名、移动、复制、删除，以及 `DriveListModel` / `FolderTreeModel` 刷新 |
| `TransferManager` | `clients/desktop/src/managers/` | 上传/下载任务编排、重试、暂停、取消、进度汇总，驱动 `UploadTaskModel` / `DownloadTaskModel` |
| `ShareManager` | `clients/desktop/src/managers/` | 所有者分享创建/查询/更新/取消，以及访客分享浏览上下文协调 |
| `TrashManager` | `clients/desktop/src/managers/` | 回收站列表、恢复、彻底删除、清空回收站，驱动 `TrashListModel` / `BatchResultModel` |
| `ProfileManager` | `clients/desktop/src/managers/` | 用户资料、密码修改、存储统计、头像/昵称更新 |

### 5.2 会话子组件关系

| 类名 | 职责 | 与 `SessionStore` 的关系 |
|------|------|-------------------------|
| `OwnerSessionManager` | JWT 域状态迁移、刷新排队、登出清理 | 作为 `SessionStore` 内部子组件 |
| `VisitorSessionManager` | Share Token 域验证、重新验证、关闭分享清理 | 作为 `SessionStore` 内部子组件 |

### 5.3 Model 类清单

| 类名 | 基类 | 冻结职责 |
|------|------|----------|
| `DriveListModel` | `QAbstractListModel` | 文件/文件夹混合列表，元素为 `DriveItem` |
| `FolderTreeModel` | `QAbstractItemModel` | 目录树与树节点展开状态 |
| `UploadTaskModel` | `QAbstractListModel` | 活跃上传任务列表 |
| `DownloadTaskModel` | `QAbstractListModel` | 活跃下载任务列表 |
| `ShareListModel` | `QAbstractListModel` | 所有者分享记录列表 |
| `TrashListModel` | `QAbstractListModel` | 回收站项目列表 |
| `BatchResultModel` | `QAbstractListModel` | 批量操作逐项结果列表 |

### 5.4 模型绑定规则

| 规则 | 冻结要求 |
|------|----------|
| 列表统一入口 | 页面不得直接消费 `QVector<QVariantMap>`；统一消费 `QAbstractListModel` |
| 值对象边界 | `DriveItem` 是规范值对象；列表模型只暴露 roles，不透出后端 JSON 结构 |
| 目录树例外 | `FolderTreeModel` 使用 `QAbstractItemModel`，不强行压平为列表 |
| 批量结果 | 逐项结果通过 `BatchResultModel` 交给 QML，不能压扁成单条 toast |

---

## 6. 命名规范冻结

| 类别 | 规范 | 示例 |
|------|------|------|
| C++ 类名 | PascalCase | `DriveManager`、`SessionStore`、`BatchResultModel` |
| C++ 方法名 | PascalCase | `GetDriveList`、`HandleTokenExpired`、`RefreshVisitorSession` |
| C++ 私有成员 | `m_` + snake_case | `m_access_token`、`m_session_state`、`m_drive_list_model` |
| C++ target name | kebab-case | `desktop-client`、`desktop-unit-tests`、`desktop-quick-tests` |
| QML 文件名 | PascalCase | `DriveBrowserPage.qml`、`ShareBrowsePage.qml`、`OwnerShell.qml` |
| QML 属性 / signal | camelCase | `pageState`、`canDownload`、`sessionExpired()` |

补充约束：

1. C++ 命名沿用后端风格，不引入 camelCase 公共方法。
2. QML 页面文件名必须与 04 号文档中的页面命名保持一一对应。
3. Manager / Model 类名必须显式包含业务域，禁止出现 `CommonManager`、`DataModel` 之类的泛化命名。

---

## 7. 测试架构与默认实施策略

### 7.1 默认实施策略

| 任务类型 | 默认策略 |
|------|----------|
| 可执行桌面功能开发 | 采用 TDD（Red → Green → Refactor）作为默认实现策略；先写失败测试，再写最小实现，再做重构 |
| 文档任务 | 仅执行 `document-validation checks`，包括 Markdown 结构、链接、术语、引用、证据 grep 等，不触发 Qt 编译 |
| 已完成实现波次 | 执行 full-regression：后端 `disk-test` + 既有集成测试保持不变；若桌面已启用则追加 `desktop-unit-tests` 与 `desktop-quick-tests` |

### 7.2 测试框架与目标布局

| 目标 | 框架 | 范围 | 说明 |
|------|------|------|------|
| `desktop-unit-tests` | `Qt Test` | C++ only | 以 managers/models/session/navigation helper 为主，不加载完整 QML Shell |
| `desktop-quick-tests` | `Qt Quick Test` | QML 页面与导航行为 | 使用 C++ helper + mock network，验证页面状态、Shell 切换、导航与动作可见性 |

### 7.3 关键区域测试族冻结

下表要求每个关键区域都至少有一个单元测试族与一个 Qt Quick Test 测试族：

| 关键区域 | 单元测试族（`Qt Test`） | Qt Quick Test 测试族 | 覆盖重点 |
|------|------------------------|----------------------|----------|
| 认证 / 会话 | `DesktopAuth`、`DesktopSession` | `DesktopShellAuthFlow`、`DesktopNavigationAuthGuard` | 登录、刷新、登出、访客验证、Shell 切换、认证守卫 |
| 模型 | `DesktopModel`、`DesktopMapper` | `DesktopDrivePageState`、`DesktopModelBinding` | `DriveItem` 归一化、`QAbstractListModel` roles、空态/错误态绑定 |
| 导航 | `DesktopNavigationState`、`DesktopShellController` | `DesktopNavigation`、`DesktopShell` | `OwnerShell` / `VisitorShell` 分离、StackView 行为、返回路径、路由守卫 |
| 传输 | `DesktopTransfer`、`DesktopTransferRetry` | `DesktopTransferPage`、`DesktopTransferNavigation` | 上传/下载状态迁移、暂停/继续/取消、传输中心页面行为 |

### 7.4 managers / models 单元测试要求

| 类别 | 强制覆盖点 |
|------|------------|
| managers | `DriveManager`、`TransferManager`、`ShareManager`、`TrashManager`、`ProfileManager` 的成功路径、错误路径、分页/刷新、状态广播 |
| auth | `AuthService`、`SessionStore`、`OwnerSessionManager`、`VisitorSessionManager` 的状态迁移与恢复 |
| models | 各 `QAbstractListModel` 的 `rowCount`、`roleNames`、增量更新、空数据与批量结果渲染前数据面 |

### 7.5 页面 / 导航 Qt Quick Test 要求

| 页面或外壳 | 强制覆盖点 |
|------------|------------|
| `OwnerShell.qml` | Sidebar 切换、StackView 替换与 push/pop、登录后默认首页 |
| `VisitorShell.qml` | 访客验证页到分享浏览页的切换、关闭分享后的清理 |
| `DriveBrowserPage.qml` | `Loading / Empty / Content / Error / BatchResult` 五态切换 |
| `TransferCenterPage.qml` | 上传/下载任务列表渲染、状态按钮显隐、错误提示 |
| `ShareBrowsePage.qml` | 访客下载权限显隐、令牌过期后的重新验证跳转 |

### 7.6 Fixture 与 Mock 策略

| 项目 | 冻结要求 |
|------|----------|
| JSON fixtures | 使用 `clients/desktop/tests/fixtures/json/` 存放 mock API 响应，按 `auth / models / navigation / transfers` 分目录 |
| 单元测试网络层 | 使用 mock network layer，替代真实 `QNetworkAccessManager` / reply |
| Qt Quick Test helper | 通过 `QuickTestBootstrap` 注入假 manager、假 model、假 session 状态，不直接连真实后端 |
| 错误回放 | 关键错误码（`40108`、`40110`、`50004`、`60004`）必须有稳定 fixture |

### 7.7 CTest 注册规则

| 项目 | 冻结要求 |
|------|----------|
| 注册方式 | 桌面测试统一注册到 CTest；`desktop-unit-tests` 与 `desktop-quick-tests` 使用 `add_test()` 暴露到顶层测试入口，语义上等价于后端 `gtest_discover_tests()` 的统一发现/执行模式 |
| 运行方式 | `ctest` 可以在 `DISK_ENABLE_DESKTOP=ON` 时同时发现后端测试与桌面测试 |
| 无界面执行 | Linux 默认使用无头平台（如 `offscreen`）运行 Qt Quick Test，避免要求桌面会话 |

---

## 8. 实施波次与回归要求

### 8.1 推荐波次

| 波次 | 实施内容 | 先写的测试 |
|------|----------|------------|
| Wave 1 | `Application`、`ShellController`、`AuthService`、`SessionStore`、登录/恢复入口 | `DesktopAuth`、`DesktopSession`、`DesktopShellAuthFlow` |
| Wave 2 | `DriveManager`、`DriveListModel`、`FolderTreeModel`、`DriveBrowserPage` | `DesktopModel`、`DesktopMapper`、`DesktopDrivePageState` |
| Wave 3 | `ShareManager`、`TrashManager`、`ProfileManager`、`OwnerShell`/`VisitorShell` 路由联动 | `DesktopNavigation`、`DesktopShell`、相关页面 quick suites |
| Wave 4 | `TransferManager`、`UploadTaskModel`、`DownloadTaskModel`、`TransferCenterPage` | `DesktopTransfer`、`DesktopTransferRetry`、`DesktopTransferPage` |

### 8.2 回归规则

1. 每个波次完成后必须先通过该波次新增测试，再执行 full-regression。
2. full-regression 不允许跳过既有后端测试；桌面是可选构建，不是替代后端测试。
3. 文档-only 变更不触发 Qt 编译，但必须保留引用、术语、结构与证据文件校验。

---

## 9. 构建与运行

### 9.1 前置依赖

| 依赖 | 最低版本 | 说明 |
|------|----------|------|
| Qt 6 | 6.2+ | `Core`、`Gui`、`Quick`、`QuickControls2`、`Network`、`Test` 模块 |
| CMake | 3.20+ | 同后端要求 |
| Clang | 21.1.6+ | Linux 平台编译器 |
| `VCPKG_ROOT` | — | 环境变量，指向 vcpkg 安装路径 |

**Qt 6 安装提示**：若 Qt 未安装在系统默认路径，需在 CMake 配置时设置 `CMAKE_PREFIX_PATH` 指向 Qt 安装目录：

```bash
# 示例：Qt 安装在 /opt/Qt/6.8.0/gcc_64
export CMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64
# 或在 configure 命令中追加
cmake --preset linux-debug-clang-desktop -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64
```

### 9.2 Configure

```bash
# 推荐：使用桌面专用 preset（自动启用 DISK_ENABLE_DESKTOP=ON）
cmake --preset linux-debug-clang-desktop

# 或手动追加开关到现有 preset
cmake --preset linux-debug-clang -DDISK_ENABLE_DESKTOP=ON
```

| 命令 | 说明 |
|------|------|
| `cmake --preset linux-debug-clang-desktop` | Linux Debug + Desktop（推荐） |
| `cmake --preset linux-release-clang-desktop` | Linux Release + Desktop |
| `cmake --preset windows-debug-clang-cl-desktop` | Windows Debug + Desktop（需 MSVC + Qt6） |
| `cmake --preset windows-release-clang-cl-desktop` | Windows Release + Desktop |

### 9.3 Build

```bash
# 构建后端 + 桌面客户端
cmake --build --preset linux-debug-clang --target disk desktop-client

# 仅构建后端（不依赖 Qt）
cmake --build --preset linux-debug-clang --target disk

# 构建桌面测试
cmake --build --preset linux-debug-clang --target desktop-unit-tests desktop-quick-tests
```

### 9.4 Test

```bash
# 使用桌面 preset 运行全部测试（后端 + 桌面）
ctest --preset linux-debug-clang-desktop

# 使用非桌面 preset（仅后端测试，无需 Qt）
ctest --preset linux-debug-clang
```

### 9.5 Run

```bash
# 运行桌面客户端
./build/linux-debug-clang-desktop/desktop-client

# 运行后端服务
./build/linux-debug-clang-desktop/disk
```

### 9.6 Linux 打包指南

桌面客户端当前以本地构建产物形式分发。推荐打包方式：

| 方式 | 说明 |
|------|------|
| AppImage | 使用 `linuxdeployqt` 将 `desktop-client` 与 Qt 依赖打包为单文件 AppImage |
| DEB / RPM | 将二进制、`.desktop` 文件、图标打包为系统包，依赖 `libqt6quickwidgets6` 等 |
| 手动部署 | 复制二进制并确保目标系统安装了匹配版本的 Qt 6 运行时库 |

**打包前提**：使用 `linux-release-clang-desktop` preset 构建 Release 版本（`-O3` 优化）。

### 9.7 Windows 就绪说明

- Windows 构建使用 `windows-debug-clang-cl-desktop` / `windows-release-clang-cl-desktop` preset
- 需设置 `QT6_INSTALL_DIR` 环境变量指向 Qt 6 MSVC 安装路径（preset 中已配置 `CMAKE_PREFIX_PATH: $env{QT6_INSTALL_DIR}`）
- Windows preset 使用 `sccache` 作为编译缓存（对应 Linux 的 `ccache`）
- 首版验收仅要求 Windows **编译成功**（DOD-10），不要求端到端测试通过

---

## 10. Manager 信号一览

以下为实际实现中各 Manager 暴露给 QML 的 signals，用于驱动页面状态和 UI 更新。

### 10.1 DriveManager

| 信号 | 参数 | 触发场景 |
|------|------|----------|
| `apiError` | `(message, code)` | 任何 API 错误 |
| `fileDetailLoaded` | `(detail: QVariantMap)` | 文件详情加载完成 |
| `breadcrumbLoaded` | `(breadcrumb: QVariantList)` | 面包屑路径加载完成 |
| `operationSuccess` | `(message)` | 创建文件夹/重命名/移动/复制/删除成功 |
| `paginationLoaded` | `(page, totalPages, total)` | 列表/搜索分页信息加载 |

### 10.2 ProfileManager

| 信号 | 参数 | 触发场景 |
|------|------|----------|
| `apiError` | `(message, code)` | 任何 API 错误 |
| `userProfileChanged` | — | 用户资料加载或更新完成 |
| `storageStatsChanged` | — | 存储统计加载完成 |
| `operationSuccess` | `(message)` | 修改资料/密码成功 |

### 10.3 TransferManager

| 信号 | 参数 | 触发场景 |
|------|------|----------|
| `apiError` | `(message, code)` | 任何 API 错误 |
| `operationSuccess` | `(message)` | 上传/下载操作成功 |

> TransferManager 同时驱动 `UploadTaskModel` 和 `DownloadTaskModel`，任务状态变化通过模型的 `dataChanged` 信号传播。

### 10.4 ShareManager

| 信号 | 参数 | 触发场景 |
|------|------|----------|
| `apiError` | `(message, code)` | 任何 API 错误 |
| `operationSuccess` | `(message)` | 更新分享设置成功 |
| `shareDetailLoaded` | `(detail: QVariantMap)` | 分享详情加载完成 |
| `paginationLoaded` | `(page, totalPages, total)` | 分享列表分页信息加载 |
| `batchResultReady` | — | 批量取消分享完成，`batchResultModel` 已更新 |
| `browseLoaded` | `(shareId)` | 分享浏览内容加载完成 |
| `shareCreated` | `(shareId, shareLink)` | 分享创建成功 |

### 10.5 TrashManager

| 信号 | 参数 | 触发场景 |
|------|------|----------|
| `apiError` | `(message, code)` | 任何 API 错误 |
| `operationSuccess` | `(message)` | 列表操作成功 |
| `paginationLoaded` | `(page, totalPages, total)` | 回收站列表分页信息加载 |
| `batchResultReady` | — | 批量恢复/删除完成，`batchResultModel` 已更新 |
| `clearAllCompleted` | `(deletedCount, freedSpace)` | 清空回收站完成 |

---

## 11. QML 上下文属性映射

`Application::Initialize()` 向 QML 引擎注册以下上下文属性，QML 页面可直接通过名称访问：

| 上下文属性名 | C++ 类型 | 说明 |
|-------------|---------|------|
| `shellController` | `ShellController` | Shell 切换、路由守卫 |
| `sessionStore` | `SessionStore` | Owner/Visitor 会话状态 |
| `driveManager` | `DriveManager` | 文件浏览、搜索、操作 |
| `profileManager` | `ProfileManager` | 用户资料、存储统计 |
| `transferManager` | `TransferManager` | 上传/下载任务管理 |
| `shareManager` | `ShareManager` | 分享管理与浏览 |
| `trashManager` | `TrashManager` | 回收站操作 |

---

## 12. 参考资料

| 文档 / 资料 | 路径 / 链接 |
|------|-------------|
| 桌面端范围与成功标准 | `docs/desktop/00-scope-and-success.md` |
| 桌面端领域模型与错误分类 | `docs/desktop/02-domain-models-and-errors.md` |
| 桌面端认证、网络与传输状态机 | `docs/desktop/03-auth-network-and-transfers.md` |
| 桌面端导航模型与页面状态 | `docs/desktop/04-navigation-and-ui-state.md` |
| 根构建文件 | `CMakeLists.txt` |
| 构建预设 | `CMakePresets.json`（含 `linux-debug-clang-desktop` 等 preset） |
| 桌面端构建文件 | `clients/desktop/CMakeLists.txt` |
| 桌面端测试构建 | `clients/desktop/tests/CMakeLists.txt` |
| 后端测试构建 | `test/CMakeLists.txt` |
| Qt 6 CMake 集成 | `https://doc.qt.io/qt-6/cmake-get-started.html` |
| QML / C++ 边界 | `https://doc.qt.io/qt-6/qtqml-cppintegration-overview.html` |
| Qt Quick Test | `https://doc.qt.io/qt-6/qtquicktest-index.html` |
| Qt Test | `https://doc.qt.io/qt-6/qtest-overview.html` |
