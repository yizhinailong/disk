# 桌面客户端 — 导航模型与页面状态

## 1. 文档说明

### 1.1 文档目的

本文档定义桌面客户端的顶层导航架构、页面清单、路由守卫、页面状态模型以及所有者模式与访客模式下的动作可见性矩阵。目标是把 00 至 03 号文档中已冻结的功能编号、认证状态机、领域模型映射到具体的页面结构和交互状态，为后续 QML 组件划分和 ViewModel 设计提供唯一导航基线。

### 1.2 权威来源

| 来源 | 路径 | 用途 |
|------|------|------|
| 范围与术语基线 | `docs/desktop/00-scope-and-success.md` | 所有者流 / 访客流、功能编号 O-01 ~ O-29 / V-01 ~ V-04 |
| 后端能力映射 | `docs/desktop/01-backend-capability-map.md` | 接口路由、分页、批量响应形态 |
| 领域模型与错误分类 | `docs/desktop/02-domain-models-and-errors.md` | `DriveItem`、`OwnerSession`、`ShareVisitorSession`、`BatchActionResult`、`ApiError` |
| 认证、网络与传输状态机 | `docs/desktop/03-auth-network-and-transfers.md` | Owner Session 状态机、Visitor Session 状态机、single-flight refresh |

### 1.3 导航不变量

1. 桌面端 **同一时刻只有一个活跃 Shell**：所有者 Shell 或访客 Shell，不会并存。
2. 所有者 Shell 使用 `StackView` 管理页面栈；访客 Shell 是独立入口流，不进入所有者页面栈。
3. 未通过认证的导航请求必须被路由守卫拦截并重定向到对应的认证页面。
4. 访客页面 **不得暴露** 任何仅限所有者的动作（上传、分享管理、回收站、设置等）。
5. 批量操作结果必须逐项展示，不允许压扁为单一成功/失败提示。

---

## 2. Shell 架构

### 2.1 所有者 Shell（Owner Shell）

所有者 Shell 是已注册用户登录后的主界面容器，使用 Qt Quick Controls `StackView` 管理页面导航。

#### 2.1.1 Shell 结构

```
OwnerShell
├── Sidebar                    # 左侧导航栏（固定可见）
│   ├── 文件浏览入口
│   ├── 传输中心入口
│   ├── 分享管理入口
│   ├── 回收站入口
│   └── 设置入口
├── Header                     # 顶部栏（面包屑 / 搜索框 / 用户信息）
└── StackView                  # 页面内容区域
    ├── DriveBrowserPage       # 默认首页
    ├── TransferCenterPage
    ├── ShareManagementPage
    ├── TrashPage
    └── SettingsPage           # 包含个人资料、密码修改、存储统计（O-26 ~ O-29）
```

> **实现说明**：`ProfilePage` 的功能（O-26 ~ O-29）已合并到 `SettingsPage` 中实现，不再作为独立页面存在。

#### 2.1.2 导航规则

| 规则 | 说明 |
|------|------|
| 首页 | 登录成功后进入 `DriveBrowserPage`（根目录） |
| Sidebar 切换 | 点击 Sidebar 条目替换 StackView 栈顶页面，不累积中间页面 |
| 文件夹钻入 | 在 `DriveBrowserPage` 内点击文件夹，StackView push 新的 `DriveBrowserPage`（新 parent_id） |
| 返回 | StackView pop 回上一级 `DriveBrowserPage`，恢复滚动位置 |
| 搜索结果 | 在当前 `DriveBrowserPage` 上层 push 搜索结果视图，搜索清除后 pop |
| 文件详情 | push 文件详情页，关闭后 pop |
| 分享创建对话框 | 以模态层叠在当前页面之上，不走 StackView |
| 批量操作确认 | 以模态层叠在当前页面之上，不走 StackView |

### 2.2 访客 Shell（Visitor Shell）

访客 Shell 是通过分享链接进入的独立浏览/下载流，不依赖所有者登录，不共享所有者 Shell 的导航结构。

#### 2.2.1 Shell 结构

```
VisitorShell
├── ShareVerifyPage            # 密码验证（如有密码保护）
└── ShareBrowsePage            # 分享内容浏览 + 下载
```

#### 2.2.2 导航规则

| 规则 | 说明 |
|------|------|
| 入口 | 从系统协议唤起或 URL 打开分享链接，提取 `share_id` |
| 验证 | 若分享有密码保护，先展示 `ShareVerifyPage`；无密码则自动跳过 |
| 浏览 | 验证成功后进入 `ShareBrowsePage`，展示根级分享内容 |
| 文件夹钻入 | `ShareBrowsePage` 内点击文件夹，加载子级内容（同一页面内刷新列表） |
| 下载 | 点击文件触发下载（仅当 `permission == download`） |
| 关闭 | 用户关闭分享窗口/视图，清理 `ShareVisitorSession`，回到 `Idle` |
| 无 Sidebar | 访客 Shell 不展示所有者 Sidebar |

---

## 3. 页面清单

### 3.1 所有者页面

| 页面 ID | 页面名称 | 功能编号 | 说明 |
|---------|---------|----------|------|
| P-Login | LoginPage | O-01, O-02 | 注册/登录表单 |
| P-Drive | DriveBrowserPage | O-05, O-06, O-07, O-10, O-11, O-12, O-13, O-14 | 文件浏览主页面，承载列表、搜索、详情、重命名、移动、复制、删除、创建文件夹 |
| P-Tree | FolderTreePanel | O-15 | 左侧栏目录树面板（非独立页面，嵌入 DriveBrowserPage） |
| P-Breadcrumb | BreadcrumbBar | O-16 | 顶部面包屑导航栏（非独立页面，嵌入 DriveBrowserPage） |
| P-Upload | UploadFlow | O-08 | 上传对话框/面板，从 DriveBrowserPage 触发 |
| P-Download | DownloadFlow | O-09 | 下载对话框/面板，从 DriveBrowserPage 触发 |
| P-Transfer | TransferCenterPage | O-08, O-09 | 传输中心：上传/下载任务列表及进度 |
| P-Share | ShareManagementPage | O-17, O-18, O-19, O-20, O-21 | 分享管理：创建、列表、详情、更新、取消 |
| P-Trash | TrashPage | O-22, O-23, O-24, O-25 | 回收站：列表、恢复、彻底删除、清空 |
| P-Settings | SettingsPage | O-26, O-27, O-28, O-29 | 设置/个人资料：查看、修改、密码变更、存储统计 |

### 3.2 访客页面

| 页面 ID | 页面名称 | 功能编号 | 说明 |
|---------|---------|----------|------|
| P-Verify | ShareVerifyPage | V-01, V-02 | 分享链接入口 + 密码验证 |
| P-Browse | ShareBrowsePage | V-03, V-04 | 分享内容浏览 + 文件下载 |

### 3.3 认证前页面

| 页面 ID | 页面名称 | 说明 |
|---------|---------|------|
| P-Splash | SplashPage | 应用启动画面，检查本地令牌、决定进入 Login 或 Owner Shell |

---

## 4. 路由守卫

### 4.1 所有者路由守卫

路由守卫在页面导航前执行认证状态检查，确保只有合法会话才能进入受保护页面。

| 守卫规则 | 检查条件 | 拦截动作 |
|----------|---------|---------|
| Owner Session 活跃检查 | `OwnerSession` 状态为 `Active` | 放行 |
| Owner Session 刷新中 | `OwnerSession` 状态为 `Refreshing` | 放行（等待刷新结果，不阻断导航） |
| Owner Session 未认证 | `OwnerSession` 状态为 `LoggedOut` / `ReauthRequired` | 重定向到 `P-Login` |
| Owner Session 正在认证 | `OwnerSession` 状态为 `Authenticating` | 阻断导航，等待认证结果 |
| Owner Session 登出中 | `OwnerSession` 状态为 `LogoutPending` | 阻断导航，等待清理完成 |

#### 4.1.1 路由守卫决策表

| 导航目标 | 前置条件 | 守卫结果 |
|----------|---------|---------|
| `P-Login` → 任意所有者页面 | 登录成功，`OwnerSession` 进入 `Active` | 放行到 `P-Drive` |
| `P-Splash` → `P-Drive` | 本地存在有效 access_token 或 refresh_token | 尝试恢复会话；失败则重定向到 `P-Login` |
| `P-Splash` → `P-Login` | 本地无令牌或令牌恢复失败 | 直接进入 `P-Login` |
| `P-Drive` → `P-Transfer` | `OwnerSession` 为 `Active` | 放行 |
| `P-Drive` → `P-Share` | `OwnerSession` 为 `Active` | 放行 |
| `P-Drive` → `P-Trash` | `OwnerSession` 为 `Active` | 放行 |
| `P-Drive` → `P-Settings` | `OwnerSession` 为 `Active` | 放行 |
| 任意所有者页面 | 收到 `40105 InvalidRefreshToken` / `40110 RefreshTokenAlreadyUsed` | 清空 `OwnerSession`，重定向到 `P-Login` |

### 4.2 访客路由守卫

| 守卫规则 | 检查条件 | 拦截动作 |
|----------|---------|---------|
| Visitor Session 活跃检查 | `ShareVisitorSession` 状态为 `Active` | 放行到 `P-Browse` |
| Visitor Session 未验证 | `ShareVisitorSession` 状态为 `Idle` / `Unverified` | 重定向到 `P-Verify` |
| Visitor Session 验证中 | `ShareVisitorSession` 状态为 `Verifying` | 阻断导航，等待验证结果 |
| Visitor Token 过期 | `ShareVisitorSession` 状态为 `ReverifyRequired` | 重定向到 `P-Verify` |
| 分享失效 | 收到 `60001 ShareNotFound` / `60002 ShareExpired` | 关闭访客 Shell，提示分享已失效 |

### 4.3 跨 Shell 隔离规则

| 规则 | 说明 |
|------|------|
| 所有者页面不可从访客 Shell 进入 | 访客 Shell 不包含所有者 Sidebar，无法导航到 `P-Transfer`、`P-Share`、`P-Trash`、`P-Settings` |
| 访客页面不可从所有者 Shell 进入 | 分享浏览/下载只在访客 Shell 中进行；所有者通过浏览器等外部工具访问分享链接 |
| 认证域不交叉 | 所有者令牌不注入访客请求，访客令牌不注入所有者请求（参见 03 号文档 §2.1） |

---

## 5. 页面状态模型

每个页面拥有统一的状态机，用于控制内容区域的呈现。页面状态由数据加载结果和用户操作共同驱动。

### 5.1 通用页面状态

| 状态 | 含义 | 触发条件 |
|------|------|---------|
| `Loading` | 正在加载初始数据 | 页面首次进入或用户触发刷新 |
| `Empty` | 数据加载成功但结果为空 | 列表接口返回空 `items` |
| `Content` | 数据加载成功且有内容 | 列表接口返回非空 `items` |
| `Error` | 数据加载失败 | 网络错误或后端返回错误码 |
| `BatchResult` | 批量操作完成（可能部分成功） | 批量操作接口返回 `BatchActionResult` |

### 5.2 状态迁移

```
Loading ──成功且有数据──→ Content
Loading ──成功但无数据──→ Empty
Loading ──失败──────────→ Error
Content ──用户刷新──────→ Loading
Content ──批量操作──────→ BatchResult
Empty ────用户刷新──────→ Loading
Error ────用户重试──────→ Loading
BatchResult ──用户确认──→ Loading（刷新列表）
```

### 5.3 各页面状态详情

#### P-Login（LoginPage）

| 状态 | 页面表现 |
|------|---------|
| `Loading` | 登录请求发送中，禁用表单输入和提交按钮 |
| `Error` | 显示错误提示（如 `40101 InvalidCredentials`、`40102 AccountLocked`），保持表单可编辑 |
| `Content` | 登录成功，触发导航到 `P-Drive`（不展示内容态，直接跳转） |

**无 Empty / BatchResult 状态。**

#### P-Drive（DriveBrowserPage）

| 状态 | 页面表现 |
|------|---------|
| `Loading` | 文件列表加载中，展示骨架屏或加载指示器 |
| `Empty` | 当前目录无文件/文件夹，展示空状态引导（如"此文件夹为空，点击上传添加文件"） |
| `Content` | 展示文件/文件夹列表（`DriveItem[]`），支持排序、筛选、分页 |
| `Error` | 加载失败，展示错误信息和重试按钮 |
| `BatchResult` | 批量删除/移动/复制操作完成，展示逐项结果摘要（参见 §6） |

#### P-Transfer（TransferCenterPage）

| 状态 | 页面表现 |
|------|---------|
| `Loading` | 传输任务列表加载中 |
| `Empty` | 无传输任务，展示空状态引导（如"暂无传输任务"） |
| `Content` | 展示上传/下载任务列表，每个任务显示文件名、进度、速度、状态、操作按钮 |
| `Error` | 任务列表加载失败 |

**无 BatchResult 状态。单个任务的错误状态由 `UploadTask.error` / `DownloadTask.error` 承载。**

#### P-Share（ShareManagementPage）

| 状态 | 页面表现 |
|------|---------|
| `Loading` | 分享列表加载中 |
| `Empty` | 无分享记录，展示空状态引导（如"暂无分享，选择文件创建分享"） |
| `Content` | 展示分享列表（`ShareItem[]`），支持分页和状态筛选 |
| `Error` | 列表加载失败 |
| `BatchResult` | 批量取消分享完成，展示逐项结果摘要（参见 §6） |

#### P-Trash（TrashPage）

| 状态 | 页面表现 |
|------|---------|
| `Loading` | 回收站列表加载中 |
| `Empty` | 回收站为空 |
| `Content` | 展示回收站项目列表（`TrashItem[]`），支持分页 |
| `Error` | 列表加载失败 |
| `BatchResult` | 批量恢复/彻底删除完成，展示逐项结果摘要（参见 §6） |

#### P-Settings（SettingsPage）

| 状态 | 页面表现 |
|------|---------|
| `Loading` | 用户资料/存储统计加载中 |
| `Content` | 展示用户资料（`UserProfile`）和存储统计（`StorageStats`） |
| `Error` | 加载失败 |

**无 Empty / BatchResult 状态。** 个人资料始终存在，不存在"空"场景。

#### P-Verify（ShareVerifyPage）

| 状态 | 页面表现 |
|------|---------|
| `Loading` | 分享验证请求发送中 |
| `Content` | 展示密码输入表单（仅当分享有密码保护时） |
| `Error` | 验证失败（如 `60003 SharePasswordError`），清空密码输入框 |

**无 Empty / BatchResult 状态。**

#### P-Browse（ShareBrowsePage）

| 状态 | 页面表现 |
|------|---------|
| `Loading` | 分享内容加载中 |
| `Empty` | 分享内容为空 |
| `Content` | 展示分享文件/文件夹列表（`DriveItem[]`） |
| `Error` | 加载失败或 Token 过期（`ReverifyRequired`） |

**无 BatchResult 状态。访客不支持批量操作。**

---

## 6. 批量结果展示规则

### 6.1 涉及批量操作的页面

批量操作使用 `BatchActionResult` 模型（定义于 02 号文档 §3.11），以下页面必须支持 `BatchResult` 状态。

| 页面 | 批量操作 | 操作类型 |
|------|---------|---------|
| P-Drive | 批量移动（O-11）、批量复制（O-12）、批量删除（O-13） | `file_move` / `file_copy` / `file_delete` |
| P-Share | 批量取消分享（O-21） | `share_cancel` |
| P-Trash | 批量恢复（O-23）、批量彻底删除（O-24） | `trash_restore` / `trash_delete` |

> 注意：P-Drive 的批量移动、复制、删除操作后端当前返回 `moved_count` / `copied_count` / `deleted_count`，并非 `BatchActionResult` 结构。若后续后端升级为逐项响应，桌面端应使用 `BatchResult` 状态呈现。当前版本在 P-Drive 中以轻量通知展示操作摘要。

### 6.2 BatchResult 呈现规范

| 规则 | 说明 |
|------|------|
| 全部成功 | 显示成功摘要（如"已恢复 3 个项目"），自动回到 `Content` 状态并刷新列表 |
| 部分成功 | 显示逐项结果列表，成功项和失败项分别标记；提供"查看详情"展开模式 |
| 全部失败 | 等同于 `Error` 状态，展示失败原因 |
| 逐项信息 | 每个失败项展示 `ApiError.message`，恢复操作展示 `resolved_path`，删除操作展示 `freed_space` |
| 用户确认 | 用户确认 BatchResult 后，页面进入 `Loading` 状态刷新列表 |

### 6.3 各页面 BatchResult 特定展示

#### P-Share（批量取消分享）

| 项目 | 展示内容 |
|------|---------|
| 成功项 | `share_id` + "已取消" |
| 失败项 | `share_id` + 错误消息（如"分享不存在"、"分享已过期"） |

#### P-Trash（批量恢复）

| 项目 | 展示内容 |
|------|---------|
| 成功项 | 项目名称 + 恢复后路径（`resolved_path`）+ 恢复后类型（`file` / `folder`） |
| 失败项 | 项目名称 + 错误消息 |

#### P-Trash（批量彻底删除）

| 项目 | 展示内容 |
|------|---------|
| 成功项 | 项目名称 + 释放空间（`freed_space`） |
| 失败项 | 项目名称 + 错误消息 |

#### P-Trash（清空回收站）

| 项目 | 展示内容 |
|------|---------|
| 整体结果 | 已删除数量 + 总释放空间 |

> 清空回收站（`DELETE /api/trash/all`）返回 `deleted_count` + `freed_space`，不是 `BatchActionResult`，使用轻量成功通知展示。

---

## 7. 动作可见性矩阵

### 7.1 所有者模式可用动作

以下动作仅在所有者模式（`OwnerSession` 状态为 `Active` 或 `Refreshing`）下可见。

| 动作 | 所属页面 | 功能编号 | 前置条件 |
|------|---------|----------|---------|
| 注册 | P-Login | O-01 | `OwnerSession` 为 `LoggedOut` |
| 登录 | P-Login | O-02 | `OwnerSession` 为 `LoggedOut` |
| 令牌刷新 | 自动触发 | O-03 | `OwnerSession` 收到 `40108` |
| 登出 | Sidebar / Header | O-04 | `OwnerSession` 为 `Active` |
| 文件浏览 | P-Drive | O-05 | `OwnerSession` 为 `Active` |
| 文件详情 | P-Drive | O-06 | 选中单个文件 |
| 文件搜索 | P-Drive | O-07 | 输入搜索关键词 |
| 上传文件 | P-Drive → P-Upload | O-08 | 有足够存储配额 |
| 下载文件 | P-Drive → P-Download | O-09 | 选中文件 |
| 重命名 | P-Drive | O-10 | 选中单个文件或文件夹 |
| 移动 | P-Drive | O-11 | 选中一个或多个项目 |
| 复制 | P-Drive | O-12 | 选中一个或多个项目 |
| 删除（移入回收站） | P-Drive | O-13 | 选中一个或多个项目 |
| 创建文件夹 | P-Drive | O-14 | 在当前目录下创建 |
| 目录树 | P-Tree | O-15 | 文件夹存在 |
| 面包屑导航 | P-Breadcrumb | O-16 | 非根目录 |
| 创建分享 | P-Share | O-17 | 选中一个或多个项目 |
| 分享列表 | P-Share | O-18 | 进入分享管理页 |
| 分享详情 | P-Share | O-19 | 选中单个分享 |
| 更新分享设置 | P-Share | O-20 | 选中单个活跃分享 |
| 取消分享 | P-Share | O-21 | 选中一个或多个活跃分享 |
| 回收站列表 | P-Trash | O-22 | 进入回收站页 |
| 恢复 | P-Trash | O-23 | 选中一个或多个回收站项目 |
| 彻底删除 | P-Trash | O-24 | 选中一个或多个回收站项目 |
| 清空回收站 | P-Trash | O-25 | 回收站不为空 |
| 查看个人信息 | P-Settings | O-26 | 进入设置页 |
| 修改个人信息 | P-Settings | O-27 | 编辑昵称或头像 |
| 修改密码 | P-Settings | O-28 | 输入旧密码和新密码 |
| 存储空间统计 | P-Settings | O-29 | 进入设置页 |

### 7.2 访客模式可用动作

以下动作仅在访客模式（`ShareVisitorSession` 状态为 `Active`）下可见。

| 动作 | 所属页面 | 功能编号 | 前置条件 |
|------|---------|----------|---------|
| 打开分享链接 | P-Verify | V-01 | 获得有效 `share_id` |
| 分享验证（输入密码） | P-Verify | V-02 | 分享有密码保护 |
| 浏览分享内容 | P-Browse | V-03 | `ShareVisitorSession` 为 `Active` |
| 下载分享文件 | P-Browse | V-04 | `permission == download` |

### 7.3 动作可见性对照

| 动作类别 | 所有者模式 | 访客模式 | 说明 |
|----------|-----------|---------|------|
| 文件浏览 | 可用 | 仅分享范围内 | 访客只能看到被分享的文件 |
| 文件搜索 | 可用 | 不可用 | 访客无搜索能力 |
| 文件上传 | 可用 | **不可用** | 访客为只读/下载模式 |
| 文件下载 | 可用 | 受限（需 `permission == download`） | 访客下载受分享权限控制 |
| 文件重命名 | 可用 | **不可用** | |
| 文件移动 | 可用 | **不可用** | |
| 文件复制 | 可用 | **不可用** | |
| 文件删除 | 可用 | **不可用** | |
| 创建文件夹 | 可用 | **不可用** | |
| 分享创建/管理/取消 | 可用 | **不可用** | |
| 回收站操作 | 可用 | **不可用** | |
| 个人设置 | 可用 | **不可用** | |
| 传输中心 | 可用 | **不可用** | 访客下载进度在 `P-Browse` 内展示 |
| 目录树 | 可用 | **不可用** | 访客通过面包屑或逐层进入浏览 |

### 7.4 访客下载权限降级

当 `ShareVisitorSession.permission == view` 时，访客仍可浏览分享内容，但下载动作受到以下约束：

| 条件 | 页面表现 |
|------|---------|
| `permission == view` | 下载按钮置灰或隐藏，提示"此分享仅支持查看" |
| `permission == download` | 下载按钮可用 |
| 收到 `403 60004 ShareAccessDenied` | 保持 `Active` 状态，禁用下载按钮，不中断浏览会话 |

---

## 8. 流程到页面映射

### 8.1 所有者流程映射

#### 8.1.1 登录流程（O-01 ~ O-04）

| 步骤 | 流程编号 | 页面 | 页面状态 | 说明 |
|------|---------|------|---------|------|
| 1 | O-01 | P-Login | `Content` | 展示注册表单 |
| 2 | O-02 | P-Login | `Loading` → `Content` | 提交登录 → 成功后跳转 P-Drive |
| 3 | O-03 | 自动触发 | — | Access Token 过期时 single-flight refresh |
| 4 | O-04 | Sidebar | — | 登出，清理会话，跳转 P-Login |

#### 8.1.2 文件浏览流程（O-05 ~ O-07, O-10 ~ O-14）

| 步骤 | 流程编号 | 页面 | 页面状态 | 说明 |
|------|---------|------|---------|------|
| 1 | O-05 | P-Drive | `Loading` → `Content` | 进入目录，加载文件列表 |
| 2 | O-06 | P-Drive | `Content` | 查看文件详情（侧边栏或弹窗） |
| 3 | O-07 | P-Drive | `Loading` → `Content` | 搜索关键词，展示搜索结果 |
| 4 | O-10 | P-Drive | `Content` | 重命名（行内编辑或弹窗） |
| 5 | O-11 | P-Drive | `Content` → `BatchResult` | 批量移动，选择目标目录 |
| 6 | O-12 | P-Drive | `Content` → `BatchResult` | 批量复制，选择目标目录 |
| 7 | O-13 | P-Drive | `Content` → `BatchResult` | 批量删除（移入回收站） |
| 8 | O-14 | P-Drive | `Content` | 创建文件夹（弹窗输入名称） |

#### 8.1.3 文件夹树与面包屑（O-15, O-16）

| 步骤 | 流程编号 | 页面 | 页面状态 | 说明 |
|------|---------|------|---------|------|
| 1 | O-15 | P-Tree（面板） | `Loading` → `Content` | 加载目录树，嵌入 P-Drive 左侧 |
| 2 | O-16 | P-Breadcrumb（栏） | `Content` | 加载面包屑路径，嵌入 P-Drive 顶部 |

#### 8.1.4 上传流程（O-08）

| 步骤 | 流程编号 | 页面 | 页面状态 | 说明 |
|------|---------|------|---------|------|
| 1 | O-08 | P-Upload | `Loading` → `Content` | 选择文件 → 哈希计算 → init → chunk → complete |
| 2 | — | P-Transfer | `Content` | 传输中心展示上传任务进度 |

#### 8.1.5 下载流程（O-09）

| 步骤 | 流程编号 | 页面 | 页面状态 | 说明 |
|------|---------|------|---------|------|
| 1 | O-09 | P-Download | `Loading` → `Content` | 获取元数据 → 开始下载 |
| 2 | — | P-Transfer | `Content` | 传输中心展示下载任务进度 |

#### 8.1.6 传输中心

| 步骤 | 页面 | 页面状态 | 说明 |
|------|------|---------|------|
| 1 | P-Transfer | `Loading` → `Content` / `Empty` | 展示所有上传/下载任务列表 |
| 2 | P-Transfer | `Content` | 每个任务显示：文件名、进度条、速度、剩余时间、操作按钮（暂停/继续/取消/重试） |
| 3 | P-Transfer | `Content` | 任务错误状态展示 `ApiError.message` + 重试按钮（`retryable == true` 时） |

#### 8.1.7 分享管理流程（O-17 ~ O-21）

| 步骤 | 流程编号 | 页面 | 页面状态 | 说明 |
|------|---------|------|---------|------|
| 1 | O-18 | P-Share | `Loading` → `Content` | 加载分享列表 |
| 2 | O-17 | P-Share | `Content` | 创建分享（模态弹窗，设置密码/有效期/权限） |
| 3 | O-19 | P-Share | `Content` | 查看分享详情（侧边栏或弹窗） |
| 4 | O-20 | P-Share | `Content` | 更新分享设置 |
| 5 | O-21 | P-Share | `Content` → `BatchResult` | 批量取消分享 |

#### 8.1.8 回收站流程（O-22 ~ O-25）

| 步骤 | 流程编号 | 页面 | 页面状态 | 说明 |
|------|---------|------|---------|------|
| 1 | O-22 | P-Trash | `Loading` → `Content` | 加载回收站列表 |
| 2 | O-23 | P-Trash | `Content` → `BatchResult` | 批量恢复 |
| 3 | O-24 | P-Trash | `Content` → `BatchResult` | 批量彻底删除 |
| 4 | O-25 | P-Trash | `Content` → `Content` | 清空回收站（轻量成功通知） |

#### 8.1.9 设置/个人资料流程（O-26 ~ O-29）

| 步骤 | 流程编号 | 页面 | 页面状态 | 说明 |
|------|---------|------|---------|------|
| 1 | O-26 | P-Settings | `Loading` → `Content` | 加载用户资料 |
| 2 | O-27 | P-Settings | `Content` | 修改昵称/头像 |
| 3 | O-28 | P-Settings | `Content` | 修改密码（弹窗输入旧密码和新密码） |
| 4 | O-29 | P-Settings | `Content` | 存储空间统计（环形图或进度条展示） |

### 8.2 访客流程映射

#### 8.2.1 分享访客浏览/下载（V-01 ~ V-04）

| 步骤 | 流程编号 | 页面 | 页面状态 | 说明 |
|------|---------|------|---------|------|
| 1 | V-01 | P-Verify | `Content` | 从链接提取 `share_id`，进入验证流程 |
| 2 | V-02 | P-Verify | `Loading` → `Content` / `Error` | 输入密码（如有），提交验证 |
| 3 | V-03 | P-Browse | `Loading` → `Content` | 浏览分享根级内容 |
| 4 | V-03 | P-Browse | `Loading` → `Content` | 点击文件夹进入子目录 |
| 5 | V-04 | P-Browse | `Loading` → `Content` | 点击文件触发下载（`permission == download` 时） |

---

## 9. 错误状态与页面联动

### 9.1 所有者认证错误与导航联动

| 错误场景 | 触发状态 | 导航动作 | 页面影响 |
|----------|---------|---------|---------|
| Access Token 过期（`40108`） | `OwnerSession` → `Refreshing` | 不跳转 | 页面保持当前状态，等待刷新结果 |
| 刷新成功 | `Refreshing` → `Active` | 不跳转 | 自动重放失败请求，页面恢复 |
| 刷新失败（`40105` / `40110` / `40111`） | `Refreshing` → `ReauthRequired` | 重定向到 `P-Login` | 清空所有页面栈 |
| 用户登出 | `Active` → `LogoutPending` → `LoggedOut` | 重定向到 `P-Login` | 清空所有页面栈 |
| 存储配额不足（`50004`） | 保持 `Active` | 不跳转 | 当前页面展示配额不足提示，阻止继续上传/复制 |

### 9.2 访客认证错误与导航联动

| 错误场景 | 触发状态 | 导航动作 | 页面影响 |
|----------|---------|---------|---------|
| Share Token 过期（`40108`） | `Active` → `ReverifyRequired` | 重定向到 `P-Verify` | 清空浏览页面，回到密码验证 |
| Share Token 撤销（`40111`） | `Active` → `ReverifyRequired` | 重定向到 `P-Verify` | 同上 |
| 密码错误（`60003`） | `Unverified` → `Unverified` | 保持在 `P-Verify` | 清空密码输入框，展示错误提示 |
| 分享不存在/过期（`60001` / `60002`） | 任意 | 关闭访客 Shell | 提示分享已失效 |
| 下载权限不足（`60004`） | 保持 `Active` | 不跳转 | 禁用下载按钮，浏览继续 |

---

## 10. 实施建议

1. 每个页面实现一个统一的 `PageState` 属性（`loading` / `empty` / `content` / `error` / `batchResult`），QML 根据此属性切换显示区域。
2. 路由守卫由 C++ 层在页面导航前执行，QML 不直接判断认证状态。
3. `BatchResult` 展示组件应复用 `BatchActionResult` + `BatchActionResultItem` 模型，通过 `resource_key` 关联原始项目列表。
4. 所有者 Sidebar 的当前选中状态与 StackView 栈顶页面双向绑定，确保页面切换时 Sidebar 高亮同步。
5. 访客 Shell 作为一个独立的 Window 或 StackView 顶层组件实现，与所有者 Shell 完全解耦。

---

## 11. 参考资料

| 文档 / 代码 | 路径 |
|------|------|
| 桌面端范围与成功标准 | `docs/desktop/00-scope-and-success.md` |
| 桌面端后端能力映射 | `docs/desktop/01-backend-capability-map.md` |
| 桌面端领域模型与错误分类 | `docs/desktop/02-domain-models-and-errors.md` |
| 桌面端认证、网络与传输状态机 | `docs/desktop/03-auth-network-and-transfers.md` |
| API 接口设计 | `docs/design/02-API接口设计.md` |
