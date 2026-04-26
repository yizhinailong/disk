# 桌面客户端 - 范围与成功标准

## 1. 文档说明

### 1.1 文档目的

本文档定义网盘桌面客户端首个发布版本的功能范围、平台策略和验收标准。后续所有桌面端设计文档（API 对接、架构、UI 等）均以本文档为术语和边界基线，不得自行扩展范围。

### 1.2 读者对象

- 桌面端开发人员
- 后端 API 对接人员
- 测试人员
- 项目管理者

---

## 2. 产品定义

### 2.1 产品形态

桌面客户端是一个 **原生桌面应用**，使用 C++/Qt（QML）技术栈构建，通过 HTTPS 调用网盘后端 RESTful API，为用户提供本地化的文件管理体验。

桌面客户端内部包含 **两个独立产品流**：

| 产品流 | 说明 | 认证域 |
|--------|------|--------|
| 所有者流（Owner Session） | 注册用户登录后管理自己的文件、文件夹、分享、回收站等 | JWT（Access Token + Refresh Token） |
| 访客流（Visitor Session） | 任意用户通过分享链接访问他人分享的文件，无需注册登录 | 分享令牌（Share Token） |

两个产品流共享同一个桌面应用窗口和导航框架，但认证状态、API 路由和数据模型完全独立。所有者流和访客流 **不会同时处于活跃状态**。

### 2.2 技术架构原则

| 原则 | 说明 |
|------|------|
| C++ 管理状态与业务逻辑 | 所有网络请求、令牌管理、数据模型由 C++ 层负责 |
| QML 负责 UI 呈现 | 界面、动画、布局由 QML 声明式实现，不承载业务逻辑 |
| 单进程架构 | 所有者流和访客流在同一进程中，通过状态机切换 |

---

## Platform Default

**平台默认策略（Linux-first / Windows-ready）**

### 3.1 平台优先级

| 平台 | 策略 | 说明 |
|------|------|------|
| Linux (x86_64) | 首选开发与测试平台 | 使用 Wayland/X11，对应后端仓库的 `linux-debug-clang` 构建预设 |
| Windows (x86_64) | 架构兼容，首版不阻塞发布 | 代码结构须保持跨平台，但首版验收仅要求 Linux 通过 |

**Linux-first / Windows-ready**：所有代码必须使用 Qt 跨平台 API，不得引入平台专有系统调用。Windows 构建须能在 CI 中成功编译，但首版不要求 Windows 端到端测试通过。

---

## Auth Domains

**认证域（Auth Domains）**

桌面客户端内部包含两个独立的认证域。

### 所有者认证域（Owner Auth Domain）

所有者是已注册并登录的用户，通过 JWT 令牌对与后端交互。

| 令牌类型 | 有效期 | 说明 |
|----------|--------|------|
| Access Token | 2 小时 | 携带于 `Authorization: Bearer` 请求头 |
| Refresh Token | 7 天 | 用于无感刷新 Access Token |

**桌面端职责**：

1. 安全存储令牌（使用系统密钥环或加密本地存储）
2. Access Token 过期前自动刷新，刷新失败则引导重新登录
3. 登出时清除本地令牌并调用后端黑名单接口
4. 支持多账户切换（首版可选，不阻塞发布）

### 访客认证域（Visitor Auth Domain）

访客通过分享链接访问他人文件，无需注册或登录。

| 令牌类型 | 获取方式 | 说明 |
|----------|----------|------|
| Share Token | 调用 `/api/share/access/{share_id}` 并传入分享密码（如有） | 携带于 `X-Share-Token` 请求头 |

**桌面端职责**：

1. 从分享链接中提取 `share_id`
2. 如有密码保护，弹出密码输入框
3. 获取 Share Token 后访问浏览和下载接口
4. Share Token 过期后提示用户重新验证

### 认证域隔离规则

- 所有者令牌和访客令牌 **不可混用**
- 后端对两类令牌使用不同的 Filter 校验：`JwtAuthFilter` 校验所有者请求，`ShareAuthFilter` 校验访客请求
- 桌面端通过全局状态机区分当前活跃的认证域，UI 层据此切换导航和功能入口

---

## In Scope

以下功能纳入首版范围。每项功能均须对接后端已有 API，不发明新后端接口。

### 所有者功能

| 编号 | 功能 | 对接 API | 说明 |
|------|------|----------|------|
| O-01 | 用户注册 | `POST /api/auth/register` | 用户名 + 邮箱 + 密码 |
| O-02 | 用户登录 | `POST /api/auth/login` | 支持用户名或邮箱登录 |
| O-03 | 令牌刷新 | `POST /api/auth/refresh` | Access Token 过期前自动刷新 |
| O-04 | 用户登出 | `POST /api/auth/logout` | 清除本地令牌 + 后端黑名单 |
| O-05 | 文件浏览 | `GET /api/file/list` | 分页、排序、类型筛选 |
| O-06 | 文件详情 | `GET /api/file/{file_id}` | 查看文件元信息 |
| O-07 | 文件搜索 | `GET /api/file/search` | 按文件名模糊搜索 |
| O-08 | 文件上传 | `POST /api/file/upload/init` + 分片上传 + 完成上传 | 支持分片上传、断点续传、秒传 |
| O-09 | 文件下载 | `GET /api/file/download/{file_id}` | 支持 Range 请求断点续传 |
| O-10 | 文件/文件夹重命名 | `PUT /api/file/{file_id}/rename` | `file_id` 可为文件或文件夹 ID，服务端自动判断类型；同名冲突检测 |
| O-11 | 文件/文件夹移动 | `PUT /api/file/move` | 批量移动，`file_ids` 可混合文件和文件夹 ID |
| O-12 | 文件/文件夹复制 | `POST /api/file/copy` | 批量复制，`file_ids` 可混合文件和文件夹 ID |
| O-13 | 文件/文件夹删除 | `DELETE /api/file` | 批量移入回收站，`file_ids` 可混合文件和文件夹 ID |
| O-14 | 文件夹创建 | `POST /api/folder/create` | 指定父目录，同名检测 |
| O-15 | 目录树 | `GET /api/folder/tree` | 左侧栏树形导航 |
| O-16 | 面包屑导航 | `GET /api/folder/{folder_id}/breadcrumb` | 顶部路径导航，需传入当前文件夹 ID |
| O-17 | 创建分享 | `POST /api/share` | 支持设置密码、有效期、权限 |
| O-18 | 分享列表 | `GET /api/share` | 查看已创建的分享 |
| O-19 | 分享详情 | `GET /api/share/{share_id}` | 查看单个分享详情 |
| O-20 | 分享设置更新 | `PUT /api/share/{share_id}` | 更新有效期、密码、权限 |
| O-21 | 取消分享 | `DELETE /api/share` | 批量取消 |
| O-22 | 回收站列表 | `GET /api/trash` | 分页查看已删除文件 |
| O-23 | 回收站恢复 | `POST /api/trash/restore` | 批量恢复 |
| O-24 | 回收站彻底删除 | `DELETE /api/trash` | 批量彻底删除 |
| O-25 | 清空回收站 | `DELETE /api/trash/all` | 彻底删除所有回收站项目 |
| O-26 | 个人信息查看 | `GET /api/user/profile` | 用户名、昵称、头像等 |
| O-27 | 个人信息修改 | `PATCH /api/user/profile` | 修改昵称、头像 |
| O-28 | 修改密码 | `PUT /api/user/password` | 验证旧密码 |
| O-29 | 存储空间统计 | `GET /api/user/storage` | 已用空间、配额、文件数 |

### 访客功能

| 编号 | 功能 | 对接 API | 说明 |
|------|------|----------|------|
| V-01 | 打开分享链接 | 从 URL 或系统协议唤起 | 提取 share_id |
| V-02 | 分享验证 | `POST /api/share/access/{share_id}` | 输入密码（如有）获取 Share Token |
| V-03 | 浏览分享内容 | `GET /api/share/browse/{share_id}` | 文件夹层级浏览 |
| V-04 | 下载分享文件 | `GET /api/share/download/{share_id}/{file_id}` | 下载单个文件 |

---

## Out of Scope

以下功能 **明确排除** 在首版范围之外，后续版本按需评估。

| 功能 | 排除理由 |
|------|----------|
| 离线同步（offline sync） | 需要本地数据库、冲突解决策略、增量同步协议，架构复杂度超出首版容量 |
| 实时通知（real-time notifications） | 后端尚无 WebSocket/SSE 推送能力，需后端先行改造 |
| 预览引擎（preview engine） | 文档、图片、视频预览需要专用渲染组件和格式适配，工作量独立且巨大 |
| 版本历史（version history） | 后端当前不支持文件版本管理，需数据模型扩展 |
| 协同编辑（collaborative editing） | 需要 OT/CRDT 算法和后端实时协作服务，远超首版范围 |
| 多账户同时在线 | 首版仅支持单账户登录，多账户切换为后续增强 |
| 系统托盘集成 | 首版不要求最小化到托盘、托盘菜单等桌面集成功能 |
| 自动更新 | 首版不内置自动更新机制 |
| 国际化（i18n） | 首版仅支持中文界面 |
| 拖拽上传 | 首版使用文件选择对话框上传，拖拽为后续增强 |

---

## Definition of Done

首版发布的验收标准如下。所有条件必须同时满足。

### 功能完整性

| 编号 | 标准 | 验证方式 |
|------|------|----------|
| DOD-01 | 所有者功能（O-01 至 O-29）全部可操作 | 逐项手工测试通过 |
| DOD-02 | 访客功能（V-01 至 V-04）全部可操作 | 逐项手工测试通过 |
| DOD-03 | 令牌刷新在 Access Token 过期时无感完成 | 等待令牌过期后继续操作，无登录提示 |
| DOD-04 | 分片上传支持断点续传（中断后继续） | 上传中断后重新启动客户端，继续上传 |
| DOD-05 | 文件下载支持断点续传（Range 请求） | 中断下载后继续，文件完整且哈希一致 |

### 质量标准

| 编号 | 标准 | 验证方式 |
|------|------|----------|
| DOD-06 | 无 P0/P1 级别未修复缺陷 | 缺陷跟踪系统确认 |
| DOD-07 | 所有 API 调用正确处理后端错误码（40001-60xxx 范围） | 错误场景测试覆盖 |
| DOD-08 | 无内存泄漏（上传/下载 1GB 文件后内存回收正常） | Valgrind 或 ASan 报告 |

### 平台标准

| 编号 | 标准 | 验证方式 |
|------|------|----------|
| DOD-09 | Linux x86_64 上完整功能测试通过 | 手工 + 自动化测试 |
| DOD-10 | Windows x86_64 编译成功（CMake + MSVC） | CI 构建通过 |

### 文档标准

| 编号 | 标准 | 验证方式 |
|------|------|----------|
| DOD-11 | 用户使用说明文档就绪 | 包含安装、登录、基本操作指南 |
| DOD-12 | 本文档定义的范围和术语未被后续设计文档违反 | 文档评审确认 |

---

## 8. 术语表

### 8.1 核心术语

| 术语 | 英文 | 定义 |
|------|------|------|
| 所有者 | Owner | 已注册并登录的用户，拥有文件和存储空间 |
| 访客 | Visitor | 通过分享链接访问文件的用户，无需注册 |
| 所有者会话 | Owner Session | 所有者登录后到登出之间的交互周期，使用 JWT 认证 |
| 访客会话 | Visitor Session | 访客打开分享链接到关闭之间的交互周期，使用 Share Token 认证 |
| Access Token | Access Token | 短期 JWT（2 小时），用于所有者 API 调用 |
| Refresh Token | Refresh Token | 长期 JWT（7 天），用于刷新 Access Token |
| 分享令牌 | Share Token | 访客验证分享后获取的临时令牌 |
| 分片上传 | Chunked Upload | 将文件按 5MB 分片逐个上传 |
| 断点续传 | Resume Transfer | 上传或下载中断后从断点继续 |
| 秒传 | Instant Upload | 通过文件哈希匹配已有文件，跳过数据传输 |
| 面包屑 | Breadcrumb | 顶部显示的当前目录路径，可点击跳转 |
| 目录树 | Folder Tree | 左侧栏显示的文件夹层级结构 |
| 回收站 | Trash / Recycle Bin | 已删除文件的暂存区，30 天自动清理 |
| 存储配额 | Storage Quota | 用户可使用的最大存储空间（默认 10 GB） |

### 8.2 缩写

| 缩写 | 全称 |
|------|------|
| API | Application Programming Interface |
| JWT | JSON Web Token |
| QML | Qt Modeling Language |
| HTTPS | Hypertext Transfer Protocol Secure |
| MD5 | Message Digest Algorithm 5 |
| SSD | Solid State Drive |

---

## 9. 参考资料

| 文档 | 路径 |
|------|------|
| 系统概述 | `docs/design/00-系统概述.md` |
| 功能需求规格 | `docs/design/01-功能需求规格.md` |
| API 接口设计 | `docs/design/02-API接口设计.md` |
| 数据库设计 | `docs/design/03-数据库设计.md` |
| 系统测试计划 | `docs/design/04-系统测试计划.md` |
| 后端构建预设 | `CMakePresets.json` |
