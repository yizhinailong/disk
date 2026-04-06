# Qt6/QML 桌面客户端 (ui/diskqml/)

Qt6 Quick + QuickControls2 桌面客户端。API 客户端 → Service → ViewModel → QML 视图。

## STRUCTURE

```
ui/diskqml/
├── CMakeLists.txt     # diskqml_core（静态库）+ appdiskqml（可执行）
├── src/
│   ├── main.cpp       # 入口：初始化所有 ViewModel → QML 引擎加载 Disk 模块
│   ├── api/           # HTTP API 封装（7 API 类：Auth/File/Folder/Share/Trash/User）
│   ├── services/      # 客户端业务服务（7）：Token存储/刷新协调 + 各模块服务
│   ├── viewmodels/    # QML ViewModel（10）：Login/Register/Session/FileList/Share/Trash/Transfers/Settings/User
│   ├── models/        # Qt Model 类（5）：FileList/Breadcrumb/FolderTree/TrashList/ShareListModel
│   ├── dtos/          # 客户端 DTO（4）：Auth/File/Share/Trash DTOs + ApiEnvelope
│   ├── transfers/     # 传输子系统：TransferStore/QueueModel/DownloadEngine/UploadEngine
│   ├── platform/      # 平台集成（系统托盘等）
│   └── utils/         # 工具类：ConfigStore/FormatUtils
└── qml/
    ├── Main.qml       # QML 入口
    ├── App.qml        # 应用壳
    ├── views/         # 页面视图（12）：Login/Register/Home/Files/Trash/Upload/Download/Share/Settings/User
    ├── components/    # 可复用组件：primitives（5基础组件）+ shell（3外壳组件）+ helpers
    ├── dialogs/       # 对话框（5）：NewFolder/Rename/DeleteConfirm/FolderPicker/Upload
    └── tokens/        # 设计令牌：StyleTokens.qml
```

## WHERE TO LOOK

| 任务 | 位置 |
|------|------|
| 添加新页面 | `qml/views/` + `src/viewmodels/` 对应 ViewModel |
| 修改 API 调用 | `src/api/` 对应 API 类 |
| 添加 QML 组件 | `qml/components/primitives/`（基础）或 `qml/components/shell/`（外壳） |
| 添加对话框 | `qml/dialogs/` |
| 修改传输逻辑 | `src/transfers/` — TransferStore + Engine |
| Token 管理 | `src/services/TokenStore.cpp` + `TokenRefreshCoordinator.cpp` |

## CONVENTIONS

- **`diskqml_core` vs `appdiskqml`**：非 QML 组件入 `diskqml_core`；`QML_ELEMENT` 类必须入 `appdiskqml` QML 模块
- **ViewModel 模式**：每个页面一个 ViewModel，通过 `Q_PROPERTY` 暴露数据给 QML
- **API 层无状态**：`ApiClient` 单例，Service 层管理认证状态
- **DTO 层**：`ApiEnvelope<T>` 统一响应解析

## ANTI-PATTERNS

- **禁止将 `QML_ELEMENT` 类加入 `diskqml_core`** — 只能加入 `appdiskqml`
- **禁止在 QML 中直接调用 API** — 必须通过 ViewModel
