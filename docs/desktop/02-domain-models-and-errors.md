# 桌面客户端 — 领域模型与错误分类

## 1. 文档说明

### 1.1 文档目的

本文档定义桌面客户端在 C++/Qt（QML）实现中应采用的**规范领域模型**与**显式错误分类**。目标不是镜像后端 JSON，而是把后端多个接口的异构负载归一化为一组稳定的桌面端模型，供后续状态机、网络层、ViewModel 和 QML 绑定复用。

### 1.2 权威来源

| 来源 | 路径 | 用途 |
|------|------|------|
| 范围与术语基线 | `docs/desktop/00-scope-and-success.md` | 所有者流 / 访客流、术语与范围 |
| 路由与返回形态基线 | `docs/desktop/01-backend-capability-map.md` | 按桌面交互流整理的接口语义 |
| API 契约 | `docs/design/02-API接口设计.md` | 字段、分页、错误码、批量响应 |
| 文件服务 | `src/services/FileService.hpp` | 文件、上传、下载、搜索能力 |
| 分享服务 | `src/services/ShareService.hpp` | Owner 分享与 Visitor 浏览/下载 |
| 回收站服务 | `src/services/TrashService.hpp` | 回收站列表、恢复、彻底删除 |
| 系统与日志服务 | `src/services/SystemService.hpp`、`src/services/OperationLogService.hpp` | 系统统计、日志分页差异 |
| 错误码定义 | `src/utils/ErrorCode.hpp` | 10xxx / 40xxx / 50xxx / 60xxx / 70xxx |

### 1.3 归一化规则

1. 类型名使用 **PascalCase**（如 `DriveItem`、`ApiError`），字段名使用 **snake_case**，便于未来 C++ 成员与序列化字段保持一致。
2. 表中的“建议 C++/Qt 类型”用于桌面实现命名与类型约束，不等同于后端 JSON 原样透传。
3. 桌面端**禁止**把后端原始响应直接暴露给 QML；所有列表、详情、任务、错误都必须先归一化到本文档模型。
4. 文件与文件夹的混合列表、搜索结果、分享浏览结果，统一归一到一个 `DriveItem`，而不是分别建立顶层 FileItem / FolderItem 列表模型。

---

## 2. 模型设计总则

### 2.1 建模边界

| 规则 | 说明 |
|------|------|
| 认证域隔离 | `OwnerSession` 与 `ShareVisitorSession` 是两个完全独立的会话模型，令牌、错误恢复、导航入口都不能混用 |
| 传输模型归一化 | 上传/下载任务是桌面本地调度实体，可组合本地状态与后端响应字段，但不得伪造后端不存在的业务语义 |
| 分页不强行统一 | 除常规 `{ items, pagination }` 外，`GET /api/logs` 使用 `{ items, total, page, page_size }`，不得复用同一分页解析器 |
| 部分成功显式表达 | 分享取消、回收站恢复、回收站彻底删除必须保留逐项结果，不能压扁成单个布尔值 |
| 下载模式显式表达 | 下载既可能是完整下载，也可能是 HTTP Range 续传，`DownloadTask` 必须显式记录传输模式 |

### 2.2 通用类型约定

| 语义 | 建议 C++/Qt 类型 | 说明 |
|------|------------------|------|
| 文本 | `QString` | 所有字符串 ID、路径、状态、错误消息 |
| 正整数 ID / 字节数 | `quint64` | 用户 ID、文件 ID、trash_id、size、quota |
| 整数计数 | `int` | 页码、数量、成功/失败统计 |
| 时间戳 | `QDateTime` | 所有 ISO 8601 时间字符串归一化后存储 |
| 布尔标记 | `bool` | `has_password`、`retryable`、`supports_range` |
| 列表 | `QVector<T>` | 可替换为等价顺序容器 |
| 可选字段 | `std::optional<T>` | 仅在后端并非所有场景都返回时使用 |

---

## 3. 规范实体定义

### 3.1 OwnerSession

所有者登录后形成的会话实体，只用于 JWT 认证域。

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `auth_domain` | 是 | `QString` | 客户端常量 | 固定为 `owner` |
| `access_token` | 是 | `QString` | `POST /api/auth/login` / `POST /api/auth/refresh` | 所有者 API 的 Bearer Token |
| `refresh_token` | 是 | `QString` | `POST /api/auth/login` / `POST /api/auth/refresh` | 单次使用刷新令牌，刷新后必须整体替换 |
| `token_type` | 是 | `QString` | `POST /api/auth/login` | 固定值 `Bearer` |
| `expires_in_seconds` | 是 | `int` | 登录 / 刷新响应 | Access Token 剩余秒数 |
| `user_profile` | 是 | `UserProfile` | 登录响应 `user` | 当前登录用户快照 |

### 3.2 ShareVisitorSession

访客通过分享链接验证成功后形成的会话实体，只用于分享令牌域。

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `auth_domain` | 是 | `QString` | 客户端常量 | 固定为 `visitor` |
| `share_id` | 是 | `QString` | 分享链接 / 路由参数 | 外部分享标识符（如 `sh_xxx`） |
| `share_token` | 是 | `QString` | `POST /api/share/access/{share_id}` | 访客请求头 `X-Share-Token` |
| `expires_in_seconds` | 是 | `int` | 分享访问响应 | Share Token 有效期，当前为 3600 秒 |
| `permission` | 是 | `QString` | 分享访问响应 | `view` / `download` |
| `shared_items` | 是 | `QVector<DriveItem>` | 分享访问响应 `files[]` | 分享根级项目清单，先归一为 `DriveItem` |

### 3.3 UserProfile

用户资料规范模型。为兼容登录响应与完整资料响应，采用“核心必填字段 + 扩展可选字段”模式。

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `id` | 是 | `quint64` | 登录 / `GET /api/user/profile` | 用户 ID |
| `username` | 是 | `QString` | 登录 / 用户资料 | 登录名 |
| `email` | 是 | `QString` | 登录 / 用户资料 | 邮箱 |
| `nickname` | 是 | `QString` | 登录 / 用户资料 | 昵称 |
| `storage_used` | 是 | `quint64` | 登录 / 用户资料 | 已用空间快照 |
| `storage_quota` | 是 | `quint64` | 登录 / 用户资料 | 总配额快照 |
| `avatar` | 否 | `std::optional<QString>` | `GET/PATCH /api/user/profile` | 登录阶段可能缺失 |
| `file_count` | 否 | `std::optional<int>` | `GET /api/user/profile` | 仅完整资料响应提供 |
| `folder_count` | 否 | `std::optional<int>` | `GET /api/user/profile` | 仅完整资料响应提供 |
| `created_at` | 否 | `std::optional<QDateTime>` | 注册 / 用户资料 | 创建时间 |
| `updated_at` | 否 | `std::optional<QDateTime>` | `GET/PATCH /api/user/profile` | 更新时间 |

### 3.4 StorageStats

存储统计模型以配额三字段为核心，`available` 由桌面端计算，不直接信任后端是否返回该字段。

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `used` | 是 | `quint64` | `GET /api/user/storage` | 实际已使用空间，含回收站 |
| `reserved` | 是 | `quint64` | `GET /api/user/storage` | 已预占但未完成上传的空间 |
| `quota` | 是 | `quint64` | `GET /api/user/storage` | 总配额 |
| `available` | 是 | `quint64` | 客户端计算 | `max(0, quota - used - reserved)` |
| `percentage` | 否 | `std::optional<double>` | `GET /api/user/storage` | 当前后端会返回，桌面端可用于显示 |

> 规范要求：上传前必须基于 `available` 判断剩余空间；`used` 与 `reserved` 任何一个变化都需要触发 UI 重新计算。

### 3.5 DriveItem

`DriveItem` 是桌面端**唯一**的文件/文件夹混合项目模型，用于文件列表、搜索结果、分享访问结果、分享浏览结果以及分享详情中的混合项展示。

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `id` | 是 | `quint64` | 列表 / 搜索 / 分享 | 文件或文件夹 ID |
| `kind` | 是 | `QString` | 源负载 `type` | 仅允许 `file` / `folder` |
| `name` | 是 | `QString` | 列表 / 搜索 / 分享 | 展示名称 |
| `size` | 否 | `std::optional<quint64>` | 文件列表 / 搜索 / 分享 | 仅对 `kind == file` 有业务意义 |
| `mime_type` | 否 | `std::optional<QString>` | 文件列表 / 搜索 / 详情 | 仅文件存在 |
| `hash` | 否 | `std::optional<QString>` | 文件列表 / 搜索 / 详情 | 仅文件存在 |
| `item_count` | 否 | `std::optional<int>` | 文件列表 | 仅文件夹列表接口提供 |
| `parent_id` | 否 | `std::optional<quint64>` | 文件详情 / 上传完成 / 创建文件夹 | 并非所有混合接口都返回 |
| `path` | 否 | `std::optional<QString>` | 搜索 / 文件详情 | 搜索结果与详情可展示完整路径 |
| `created_at` | 否 | `std::optional<QDateTime>` | 文件列表 / 搜索 / 详情 | 分享浏览不提供 |
| `updated_at` | 否 | `std::optional<QDateTime>` | 文件列表 / 搜索 / 详情 | 分享浏览不提供 |
| `origin` | 是 | `QString` | 客户端归一化 | `file_list` / `search` / `share_access` / `share_browse` / `share_detail` |

#### DriveItem 判别与约束

| 规则 | 说明 |
|------|------|
| 判别字段 | 以后端 `type` 归一到 `kind`，不额外猜测 |
| 文件约束 | `kind == file` 时，`size` 必须存在；`mime_type`、`hash` 按来源可选 |
| 文件夹约束 | `kind == folder` 时，`mime_type`、`hash` 必须为空；`item_count` 仅在文件列表来源可用 |
| 分享来源归一化 | 分享服务内部对文件夹使用 `size = 0` 占位，桌面端归一化为 `size = null`，避免被解释为“文件夹真实大小” |

### 3.6 FolderNode

`FolderNode` 用于目录树与面包屑；面包屑中的节点仍使用同一模型，只是 `children` 为空。

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `id` | 是 | `quint64` | `GET /api/folder/tree` / `GET /api/folder/{folder_id}/breadcrumb` | 文件夹 ID，根目录允许为 `0` |
| `name` | 是 | `QString` | 目录树 / 面包屑 | 节点名称 |
| `children` | 是 | `QVector<FolderNode>` | 目录树 | 面包屑场景固定为空列表 |

### 3.7 UploadTask

`UploadTask` 是桌面端上传调度实体，组合本地文件信息、上传会话信息和错误状态。

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `task_id` | 是 | `QString` | 客户端本地 | 桌面端本地唯一任务 ID |
| `local_path` | 是 | `QString` | 客户端本地 | 本地文件绝对路径 |
| `filename` | 是 | `QString` | 本地文件 / init 请求 | 上传文件名 |
| `file_size` | 是 | `quint64` | 本地文件 / init 请求 | 文件大小 |
| `file_hash` | 是 | `QString` | 本地计算 | MD5，用于秒传 / 断点续传 |
| `parent_id` | 是 | `quint64` | init 请求 | 目标目录 ID |
| `upload_id` | 否 | `std::optional<QString>` | `POST /api/file/upload/init` | 后端上传会话 ID |
| `chunk_size` | 否 | `std::optional<int>` | init 响应 | 分片大小 |
| `total_chunks` | 否 | `std::optional<int>` | init 响应 | 总分片数 |
| `uploaded_chunk_indices` | 是 | `QVector<int>` | init 响应 / 本地进度 | 已完成分片，用于断点续传 |
| `instant_upload` | 是 | `bool` | init 响应 | 是否命中秒传 |
| `status` | 是 | `QString` | 客户端本地 | `queued` / `hashing` / `initializing` / `uploading` / `completed` / `failed` / `cancelled` |
| `error` | 否 | `std::optional<ApiError>` | 客户端归一化 | 最近一次失败信息 |

### 3.8 DownloadTask

`DownloadTask` 是桌面端下载调度实体，必须显式支持完整下载与 Range 下载两种模式。

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `task_id` | 是 | `QString` | 客户端本地 | 本地唯一任务 ID |
| `auth_domain` | 是 | `QString` | 客户端上下文 | `owner` / `visitor` |
| `share_id` | 否 | `std::optional<QString>` | Visitor 下载上下文 | 仅分享下载需要 |
| `file_id` | 是 | `quint64` | 下载元数据 / 路由参数 | 文件 ID |
| `filename` | 是 | `QString` | 下载元数据 | 文件名 |
| `file_size` | 是 | `quint64` | 下载元数据 | 总字节数 |
| `file_hash` | 否 | `std::optional<QString>` | `GET /api/file/download/{file_id}/info` | Owner 下载预检提供；分享下载完整信息同样可获得 |
| `mime_type` | 是 | `QString` | 下载元数据 | MIME 类型 |
| `supports_range` | 是 | `bool` | 下载元数据 | 是否支持 `Range` |
| `transfer_mode` | 是 | `QString` | 客户端调度 | `full` / `range` |
| `range_start` | 否 | `std::optional<quint64>` | 客户端调度 | `transfer_mode == range` 时起始字节 |
| `range_end` | 否 | `std::optional<quint64>` | 客户端调度 | `transfer_mode == range` 时结束字节 |
| `target_path` | 是 | `QString` | 客户端本地 | 下载写入路径 |
| `received_bytes` | 是 | `quint64` | 客户端本地 | 已接收字节数 |
| `status` | 是 | `QString` | 客户端本地 | `queued` / `downloading` / `paused` / `completed` / `failed` / `cancelled` |
| `error` | 否 | `std::optional<ApiError>` | 客户端归一化 | 最近一次失败信息 |

### 3.9 ShareItem

`ShareItem` 归一化 Owner 分享创建、列表、详情、更新四类响应，支持“摘要视图”和“详情视图”共用一套模型。

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `share_id` | 是 | `QString` | 分享创建 / 列表 / 详情 / 更新 | 外部分享标识符 |
| `share_link` | 是 | `QString` | 分享创建 / 列表 / 详情 | 分享 URL |
| `permission` | 是 | `QString` | 所有分享响应 | `view` / `download` |
| `has_password` | 是 | `bool` | 列表 / 详情 / 更新 | 创建响应若返回明文密码，仅转成布尔存在性 |
| `created_at` | 否 | `std::optional<QDateTime>` | 创建 / 列表 / 详情 | 创建时间 |
| `expires_at` | 否 | `std::optional<QDateTime>` | 创建 / 列表 / 详情 / 更新 | 过期时间；永久分享允许为空 |
| `status` | 否 | `std::optional<QString>` | 列表 / 详情 | `active` / `expired` / `cancelled` |
| `view_count` | 否 | `std::optional<int>` | 列表 / 详情 | 浏览次数 |
| `download_count` | 否 | `std::optional<int>` | 列表 / 详情 | 下载次数 |
| `primary_item_name` | 否 | `std::optional<QString>` | 分享列表 `file_name` | 列表首项展示名 |
| `item_count` | 否 | `std::optional<int>` | 分享列表 `file_count` / 详情 `files.size()` | 被分享项目数 |
| `items` | 否 | `QVector<DriveItem>` | 分享详情 `files[]` / 访客访问 `files[]` | 使用 `DriveItem` 承载混合项目 |
| `updated_at` | 否 | `std::optional<QDateTime>` | 更新分享设置 | 仅更新响应提供 |

### 3.10 TrashItem

回收站项目规范模型。

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `trash_id` | 是 | `quint64` | `GET /api/trash` 的 `id` | 桌面端统一重命名为 `trash_id`，与 `original_id` 区分 |
| `kind` | 是 | `QString` | `type` | `file` / `folder` |
| `original_id` | 是 | `quint64` | 回收站列表 | 原始文件或文件夹 ID |
| `name` | 是 | `QString` | 回收站列表 | 删除前名称 |
| `size` | 是 | `quint64` | 回收站列表 | 以字节计 |
| `original_path` | 是 | `QString` | 回收站列表 | 删除前路径 |
| `deleted_at` | 是 | `QDateTime` | 回收站列表 | 删除时间 |
| `expires_at` | 是 | `QDateTime` | 回收站列表 | 自动清理时间 |

### 3.11 BatchActionResult

`BatchActionResult` 是批量操作结果的规范模型，统一 share cancel 与 trash restore/delete 两种不同摘要字段命名。

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `operation` | 是 | `QString` | 客户端上下文 | `share_cancel` / `trash_restore` / `trash_delete` |
| `total_count` | 是 | `int` | `summary.total` | 批量请求总数 |
| `success_count` | 是 | `int` | `summary.succeeded` 或 `summary.success_count` | 统一成功数 |
| `failure_count` | 是 | `int` | `summary.failed` 或 `summary.failure_count` | 统一失败数 |
| `items` | 是 | `QVector<BatchActionResultItem>` | `results[]` | 逐项结果，不能省略 |

`BatchActionResultItem` 规范字段如下：

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `resource_key` | 是 | `QString` | `share_id` 或 `trash_id` | 统一转为字符串，便于同一列表渲染 |
| `status` | 是 | `QString` | `results[].status` | `success` / `failed` |
| `restored_item_id` | 否 | `std::optional<quint64>` | trash restore | 恢复成功后返回的 `file_id` 或 `folder_id` |
| `restored_item_kind` | 否 | `std::optional<QString>` | trash restore | `file` / `folder` |
| `resolved_path` | 否 | `std::optional<QString>` | trash restore | 恢复后的最终路径 |
| `freed_space` | 否 | `std::optional<quint64>` | trash delete | 彻底删除释放的空间 |
| `error` | 否 | `std::optional<ApiError>` | share cancel / trash batch | 失败时的规范错误 |

### 3.12 ApiError

`ApiError` 是桌面端所有失败响应与批量单项错误的统一模型。

| 字段 | 必填 | 建议类型 | 来源 | 说明 |
|------|------|----------|------|------|
| `code` | 是 | `int` | 后端错误响应 | 原始业务码 |
| `family` | 是 | `QString` | 客户端映射 | `general` / `auth` / `file` / `share` / `redis` |
| `category` | 是 | `QString` | 客户端映射 | 用户可感知错误类别 |
| `message` | 是 | `QString` | 后端 `message` | 允许保留服务端文案 |
| `retryable` | 是 | `bool` | 客户端映射 | 是否允许自动或手动重试 |
| `action` | 是 | `QString` | 客户端映射 | 建议处理动作 |
| `field` | 否 | `std::optional<QString>` | 批量错误扩展字段 | 仅后端返回字段错误上下文时使用 |
| `value` | 否 | `std::optional<QString>` | 批量错误扩展字段 | 与 `field` 配套 |

---

## 4. DriveItem 归一化映射

### 4.1 混合负载到 DriveItem 的正式映射

| 来源接口 | 源字段 | `DriveItem` 字段 | 归一化规则 |
|------|------|------|------|
| `GET /api/file/list` | `id` | `id` | 直接映射 |
| `GET /api/file/list` | `type` | `kind` | `file` / `folder` 原样映射 |
| `GET /api/file/list` | `name` | `name` | 直接映射 |
| `GET /api/file/list` | `size` | `size` | 仅 `type == file` 时写入 |
| `GET /api/file/list` | `mime_type` | `mime_type` | 仅文件写入 |
| `GET /api/file/list` | `hash` | `hash` | 仅文件写入 |
| `GET /api/file/list` | `item_count` | `item_count` | 仅文件夹写入 |
| `GET /api/file/list` | `created_at` / `updated_at` | `created_at` / `updated_at` | 转为 `QDateTime` |
| `GET /api/file/search` | `FileListItem` 全部字段 | 同名字段 | 与列表规则一致 |
| `GET /api/file/search` | `path` | `path` | 直接映射 |
| `POST /api/share/access/{share_id}` | `files[].id/name/type/size` | `id/name/kind/size` | 使用分享访问结果生成初始 `DriveItem` |
| `GET /api/share/browse/{share_id}` | `items[].id/name/type/size` | `id/name/kind/size` | `kind == folder` 时把占位 `size = 0` 归一为 `null` |
| `GET /api/share/{share_id}` | `files[].id/name/type/size` | `id/name/kind/size` | 与分享访问相同，作为 `ShareItem.items` 子集 |

### 4.2 场景差异说明

| 场景 | 后端特征 | 桌面端处理 |
|------|----------|-----------|
| 文件列表 | 文件夹有 `item_count`，文件有 `mime_type/hash/size` | 保留全部区分字段 |
| 搜索结果 | 比文件列表多 `path` | `path` 仅在搜索/详情使用，不强制所有 `DriveItem` 必填 |
| 分享访问 / 分享详情 | 只有 `id/name/type/size` 最小集合 | 其余字段保持空值 |
| 分享浏览 | 当前服务端对文件夹返回 `size = 0` 占位 | 归一为 `size = null`，避免 UI 误显示“0 B 文件夹” |

---

## 5. BatchActionResult 归一化映射

### 5.1 Share Cancel

| 后端字段 | 规范字段 |
|------|------|
| `summary.total` | `total_count` |
| `summary.succeeded` | `success_count` |
| `summary.failed` | `failure_count` |
| `results[].share_id` | `items[].resource_key` |
| `results[].status` | `items[].status` |
| `results[].error` | `items[].error` |

### 5.2 Trash Restore / Trash Delete

| 后端字段 | 规范字段 |
|------|------|
| `summary.total` | `total_count` |
| `summary.success_count` | `success_count` |
| `summary.failure_count` | `failure_count` |
| `results[].trash_id` | `items[].resource_key` |
| `results[].status` | `items[].status` |
| `results[].file_id` / `folder_id` | `items[].restored_item_id` + `items[].restored_item_kind` |
| `results[].path` | `items[].resolved_path` |
| `results[].freed_space` | `items[].freed_space` |
| `results[].error` | `items[].error` |

### 5.3 桌面端处理原则

1. `success_count < total_count` 时必须显示“部分成功”，不能落成单一失败提示。
2. 所有批量结果都要保留 `items[]`，即使全部成功也不能丢弃逐项条目。
3. `resource_key` 统一为字符串后，分享与回收站批量结果可共用同一列表组件和日志记录结构。

---

## 6. ApiError 错误分类与映射

### 6.1 映射算法

1. 先按错误码前缀确定族：`10xxx`、`40xxx`、`50xxx`、`60xxx`、`70xxx`。
2. 再按“精确错误码覆盖表”覆盖默认分类。
3. 最终生成 `ApiError { code, family, category, retryable, action }`。

### 6.2 码族默认映射

| 后端码族 | `ApiError.family` | 默认 `category` | 默认用户动作 `action` | `retryable` |
|------|------|------|------|------|
| `10xxx` | `general` | `RequestError` | 修正参数、刷新列表或稍后重试 | 视具体错误码覆盖 |
| `40xxx` | `auth` | `AuthenticationError` | 刷新令牌、重新登录或重新验证分享 | 视具体错误码覆盖 |
| `50xxx` | `file` | `FileDomainError` | 修正文件操作、刷新目录、重试传输 | 视具体错误码覆盖 |
| `60xxx` | `share` | `ShareDomainError` | 重新输入密码、退出分享、禁用下载按钮 | 视具体错误码覆盖 |
| `70xxx` | `redis` | `InfrastructureError` | 指数退避重试；多次失败后提示服务暂不可用 | 是 |

### 6.3 精确错误码覆盖表

| 错误码 | 枚举 | `ApiError.category` | `action` | `retryable` | 桌面端处理 |
|------|------|------|------|------|------|
| `10001` | `InvalidParameter` | `ValidationError` | `fix_request` | 否 | 修正参数后再发起请求 |
| `10002` | `ValidationFailed` | `ValidationError` | `fix_request` | 否 | 表单高亮或保留当前页面 |
| `10003` | `ResourceNotFound` | `NotFound` | `refresh_context` | 否 | 刷新当前列表或返回上一级 |
| `10004` | `ResourceConflict` | `Conflict` | `refresh_then_retry` | 否 | 先刷新最新状态，再让用户确认是否重试 |
| `10005` | `TooManyRequests` | `RateLimited` | `wait_and_retry` | 是 | 尊重 `retry_after`，延迟重试 |
| `10006` | `InternalError` | `ServerFailure` | `retry_or_report` | 是 | 显示重试按钮 |
| `40001` | `UsernameExists` | `IdentityConflict` | `change_input` | 否 | 提示更换用户名 |
| `40002` | `EmailExists` | `IdentityConflict` | `change_input` | 否 | 提示更换邮箱 |
| `40101` | `InvalidCredentials` | `CredentialsRejected` | `reenter_credentials` | 否 | 停留登录页 |
| `40102` | `AccountLocked` | `AccountRestricted` | `wait_or_contact_support` | 否 | 提示锁定状态 |
| `40103` | `AccountDisabled` | `AccountRestricted` | `contact_support` | 否 | 禁止自动重试 |
| `40104` | `InvalidToken` | `SessionExpired` | `refresh_owner_session` | 是 | 所有者流先尝试刷新一次 |
| `40105` | `InvalidRefreshToken` | `ReLoginRequired` | `clear_session_and_login` | 否 | 清空 OwnerSession |
| `40106` | `TokenMissing` | `AuthProtocolError` | `rebuild_request` | 否 | 修复请求头组装逻辑 |
| `40107` | `TokenMalformed` | `AuthProtocolError` | `clear_session_and_login` | 否 | 丢弃本地损坏令牌 |
| `40108` | `TokenExpired` | `SessionExpired` | `refresh_owner_session_or_reverify_share` | 是 | Owner 走刷新；Visitor 走重新验证 |
| `40109` | `TokenWrongType` | `AuthProtocolError` | `switch_auth_domain` | 否 | 强制检查 Owner/Visitor 令牌是否混用 |
| `40110` | `RefreshTokenAlreadyUsed` | `ReLoginRequired` | `clear_session_and_login` | 否 | 不可继续自动刷新 |
| `40111` | `TokenRevoked` | `ReLoginRequired` | `clear_session_and_login` | 否 | 令牌已失效，直接回登录 |
| `50001` | `InvalidFilename` | `FileConstraint` | `change_input` | 否 | 修改文件名 |
| `50002` | `FileTypeNotAllowed` | `FileConstraint` | `change_input` | 否 | 选择允许的文件类型 |
| `50003` | `FileSizeExceeded` | `FileConstraint` | `change_input` | 否 | 选择更小文件 |
| `50004` | `StorageQuotaExceeded` | `StorageQuotaExceeded` | `show_storage_and_stop` | 否 | 展示剩余空间，阻止继续上传/复制 |
| `50005` | `FileNotFound` | `NotFound` | `refresh_context` | 否 | 文件已不存在，刷新目录 |
| `50006` | `FolderNotFound` | `NotFound` | `refresh_context` | 否 | 文件夹已失效，回退导航 |
| `50007` | `FileAlreadyExists` | `Conflict` | `rename_or_choose_target` | 否 | 让用户重命名或更换目标目录 |
| `50008` | `UploadTaskNotFound` | `UploadSessionExpired` | `restart_upload` | 是 | 重新 `init` 后再续传 |
| `50009` | `ChunkVerifyFailed` | `TransferIntegrityError` | `retry_chunk_or_restart` | 是 | 重新上传当前分片，必要时整任务重启 |
| `50010` | `FolderAlreadyExists` | `Conflict` | `rename_or_choose_target` | 否 | 处理目录重名 |
| `50011` | `FileReadError` | `TransferReadError` | `retry_download` | 是 | 支持完整下载或 Range 重试 |
| `60001` | `ShareNotFound` | `ShareUnavailable` | `close_share_entry` | 否 | 关闭访客流或标记分享失效 |
| `60002` | `ShareExpired` | `ShareExpired` | `close_share_entry` | 否 | 提示分享已过期 |
| `60003` | `SharePasswordError` | `SharePasswordRejected` | `reenter_share_password` | 否 | 清空密码输入框，允许重新输入 |
| `60004` | `ShareAccessDenied` | `PermissionDenied` | `disable_download` | 否 | 浏览可继续，下载按钮置灰 |
| `70001` | `RedisConnectionFailed` | `InfrastructureError` | `retry_with_backoff` | 是 | 视为服务端暂时不可用 |
| `70002` | `RedisOperationFailed` | `InfrastructureError` | `retry_with_backoff` | 是 | 短暂失败可重试 |
| `70003` | `RedisKeyNotFound` | `RemoteStateMissing` | `revalidate_session_or_refresh` | 是 | 会话/缓存可能丢失，先重建上下文 |

### 6.4 认证域相关错误策略

| 场景 | 触发码 | 桌面端动作 |
|------|--------|-----------|
| Owner Access Token 过期 | `40104` / `40108` | 仅允许一个刷新请求；刷新成功后重放原请求 |
| Owner Refresh Token 无效 | `40105` / `40110` / `40111` | 立即清空 `OwnerSession`，跳回登录页 |
| Visitor Share Token 过期 | `40108` / `70003` | 清空 `ShareVisitorSession`，回到分享密码验证入口 |
| 令牌混用 | `40109` | 记录为实现错误，禁止自动重试 |

---

## 7. 关键不变量

| 不变量 | 说明 |
|------|------|
| Owner / Visitor 会话分离 | `OwnerSession` 与 `ShareVisitorSession` 永不共用令牌，也不应同时处于活跃状态 |
| Logs 分页形态例外 | `GET /api/logs` 的 `data` 为 `{ items, total, page, page_size }`，与其他 `{ items, pagination }` 接口不同 |
| Batch 可能部分成功 | 分享取消、回收站恢复、回收站彻底删除必须逐项展示成功/失败 |
| 下载模式双态 | 所有者下载和访客下载都可能是完整下载或 Range 下载，`DownloadTask.transfer_mode` 不可省略 |
| `DriveItem` 是唯一混合项目模型 | 文件列表、搜索、分享访问、分享浏览统一走 `DriveItem`，不再拆成两个顶层列表模型 |

---

## 8. 实施建议

1. C++ 网络层先把原始 `data` 解析为本文档模型，再交给状态机与 QML。
2. `ApiError` 的 `action` 应作为状态机决策输入，而不是只用来显示消息。
3. 任何新增接口如果返回文件/文件夹混合集合，应优先复用 `DriveItem` 与本节映射规则。
4. 如果后续引入通用分页对象，必须保留 `OperationLogPage` 的独立解析路径，不能反向修改日志接口适配逻辑。

---

## 9. 参考资料

| 文档 / 代码 | 路径 |
|------|------|
| 桌面端范围与成功标准 | `docs/desktop/00-scope-and-success.md` |
| 桌面端后端能力映射 | `docs/desktop/01-backend-capability-map.md` |
| API 接口设计 | `docs/design/02-API接口设计.md` |
| 错误码定义 | `src/utils/ErrorCode.hpp` |
| 文件 DTO / 服务 | `src/dtos/FileDto.hpp`、`src/services/FileService.hpp` |
| 文件夹 DTO | `src/dtos/FolderDto.hpp` |
| 分享 DTO / 服务 | `src/dtos/ShareDto.hpp`、`src/services/ShareService.hpp` |
| 回收站 DTO / 服务 | `src/dtos/TrashDto.hpp`、`src/services/TrashService.hpp` |
| 日志服务 | `src/services/OperationLogService.hpp` |
