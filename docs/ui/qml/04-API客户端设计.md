# QML API 客户端设计

**Document Version:** 1.0  
**Created:** 2026-03-08  
**Author:** Sisyphus  
**Scope:** QML Desktop Client (Qt 6.8+)

---

## 1. 设计概述

### 1.1 设计目标

本文档定义 QML（Qt Quick）桌面客户端与后端 API 交互的完整契约规范，是 QML 客户端开发的权威参考。

主要目标：

1. **端点契约完整性** - 覆盖全部 32 个 QML 客户端 API 端点（不含上传端点，上传由专用传输引擎处理）
2. **字段级精度** - 每个请求/响应字段的类型、约束、来源明确
3. **认证状态机** - JWT 令牌生命周期和刷新策略
4. **错误处理一致性** - 错误码映射和用户反馈策略
5. **范围边界清晰** - 明确 QML 未实现的端点

### 1.2 架构定位

```mermaid
flowchart TD
    subgraph QML["QML Layer (Views)"]
        Views["QML Views"]
    end

    subgraph VM["ViewModels (QObject + Q_PROPERTY)"]
        ViewModels["ViewModels"]
    end

    subgraph Services["Services (Business Logic)"]
        Services["Services"]
    end

    subgraph API["API Layer (ApiClient + *Api classes)"]
        APILayer["API Layer<br/>← 本文档范围"]
    end

    subgraph HTTP["HTTP Transport (QNetworkAccessManager)"]
        HTTPT["HTTP Transport"]
    end

    subgraph Backend["Backend API Server"]
        BackendAPI["Backend API"]
    end

    Views --> ViewModels
    ViewModels --> Services
    Services --> API
    API --> HTTP
    HTTP --> Backend
```

QML/JavaScript 仅处理 UI 渲染，所有业务逻辑、API 调用和数据处理必须在 C++ 层完成。

---

## 执行范围契约 (Execution Scope Contract)

### 本迭代范围定义

本文档定义的 API 端点契约中，以下范围适用于当前开发迭代：

| 类别 | 状态 | 端点 | 理由与触发条件 |
|------|------|------|------------------|
| **分享功能** | **IN SCOPE** | 所有 Share 端点 (8个) | 核心业务功能，本次迭代完整实现 |
| **用户配置文件** | **DEFERRED** | GET/PATCH `/api/user/profile` | 登录响应已提供 profile + quota 信息，独立刷新不是关键路径 |
| **密码修改** | **DEFERRED** | PUT `/api/user/password` | 非关键路径功能 |
| **存储统计** | **DEFERRED** | GET `/api/user/storage` | 登录响应已包含配额信息，独立刷新不是关键路径 |

### 延后实现的触发条件

**User 相关端点延后理由：**
- 登录响应 (`POST /api/auth/login`) 已返回完整的用户信息，包括 `user.*` 字段（username, email, nickname, storage_quota, storage_used, created_at）
- 客户端可以在登录时一次性获取并缓存所有必要的用户信息
- 独立的配置文件刷新、密码修改、存储统计查询不是当前迭代的关键路径
- 优先实现文件管理、分享功能等核心业务场景

**未来实现触发：**
> 当用户明确提出需要独立的配置文件编辑功能、密码修改界面、或实时的存储配额刷新时，再实现 `UserService` 和 `UserApi` 的 wiring，并将这些端点添加到主应用流程中。

### 技术实现说明

- `ShareApi` 类已存在但未在 `main.cpp` 中 wiring —— **将在本次迭代中完成 wiring**
- `UserApi` 类已存在但未在 `main.cpp` 中 wiring —— **延后实现，直到上述触发条件满足**
- Share 功能的 ViewModel、Service 层将完整实现，与 API 契约完全对等

---



## 2. 响应信封规范

### 2.1 统一响应格式

所有 API 响应使用统一的信封结构：

```json
{
  "code": 0,
  "message": "success",
  "data": { ... }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `code` | `int` | 0 = 成功，非零 = 错误 |
| `message` | `string` | 人类可读的状态消息 |
| `data` | `object` | 响应载荷（失败时可能为 null） |

### 2.2 错误码范围

| 范围 | 类别 | 说明 |
|------|------|------|
| 0 | 成功 | 操作成功完成 |
| 10xxx | 通用错误 | 参数验证、资源不存在、冲突、限流 |
| 40xxx | 认证错误 | 凭证、令牌相关问题 |
| 50xxx | 文件/文件夹错误 | 文件不存在、配额超限 |
| 60xxx | 分享错误 | 分享不存在、过期、密码错误 |
| 70xxx | Redis 错误 | 内部缓存错误 |

### 2.3 完整错误码映射

| 错误码 | 名称 | 触发场景 | UI 行为 | 用户提示 |
|--------|------|----------|---------|----------|
| **通用错误** |||||
| 10001 | InvalidParameter | 参数格式错误 | 显示表单错误 | "参数格式错误，请检查输入" |
| 10002 | ValidationFailed | 输入验证失败 | 显示字段错误 | "输入验证失败" |
| 10003 | ResourceNotFound | 资源不存在 | 返回列表 | "资源不存在或已被删除" |
| 10004 | ResourceConflict | 资源冲突 | 刷新数据 | "资源已被修改，请刷新后重试" |
| 10005 | TooManyRequests | 请求过于频繁 | 显示等待 | "操作过于频繁，请稍后再试" |
| 10006 | InternalError | 服务器内部错误 | 显示重试 | "服务器错误，请稍后再试" |
| **认证错误** |||||
| 40001 | UsernameExists | 用户名已存在 | 聚焦用户名 | "用户名已被注册" |
| 40002 | EmailExists | 邮箱已存在 | 聚焦邮箱 | "邮箱已被注册" |
| 40003 | InvalidFormat | 格式无效 | 显示字段错误 | "输入格式不正确" |
| 40101 | InvalidCredentials | 凭证错误 | 聚焦密码 | "用户名或密码错误" |
| 40102 | AccountLocked | 账户锁定 | 显示等待 | "账户已锁定，请 15 分钟后重试" |
| 40103 | AccountDisabled | 账户禁用 | 跳转登录 | "账户已被禁用，请联系管理员" |
| 40104 | InvalidToken | 令牌无效 | 跳转登录 | "登录已失效，请重新登录" |
| 40105 | InvalidRefreshToken | 刷新令牌无效 | 跳转登录 | "登录已失效，请重新登录" |
| 40106 | TokenMissing | 缺少令牌 | 跳转登录 | "请先登录" |
| 40107 | TokenMalformed | 令牌格式错误 | 跳转登录 | "登录状态异常，请重新登录" |
| 40108 | TokenExpired | 令牌过期 | 自动刷新 | 触发令牌刷新流程 |
| 40109 | TokenWrongType | 令牌类型错误 | 跳转登录 | "登录状态异常，请重新登录" |
| 40110 | RefreshTokenAlreadyUsed | 刷新令牌已使用 | 跳转登录 | "登录已在其他设备使用，请重新登录" |
| 40111 | TokenRevoked | 令牌已撤销 | 跳转登录 | "登录已被注销，请重新登录" |
| **文件错误** |||||
| 50001 | InvalidFilename | 文件名无效 | 聚焦文件名 | "文件名包含非法字符" |
| 50002 | FileTypeNotAllowed | 文件类型不允许 | 显示提示 | "不支持此文件类型" |
| 50003 | FileSizeExceeded | 文件大小超限 | 显示提示 | "文件大小超出限制" |
| 50004 | StorageQuotaExceeded | 存储配额超限 | 显示配额 | "存储空间不足，请清理后重试" |
| 50005 | FileNotFound | 文件不存在 | 刷新列表 | "文件不存在或已被删除" |
| 50006 | FolderNotFound | 文件夹不存在 | 返回根目录 | "文件夹不存在或已被删除" |
| 50007 | FileAlreadyExists | 文件已存在 | 聚焦文件名 | "同名文件已存在" |
| 50010 | FolderAlreadyExists | 文件夹已存在 | 聚焦文件夹名 | "同名文件夹已存在" |
| **分享错误** |||||
| 60001 | ShareNotFound | 分享不存在 | 显示提示 | "分享不存在或已过期" |
| 60002 | ShareExpired | 分享已过期 | 显示提示 | "分享已过期" |
| 60003 | SharePasswordError | 分享密码错误 | 聚焦密码 | "分享密码错误" |
| 60004 | ShareAccessDenied | 分享访问被拒绝 | 显示提示 | "无权限访问此分享" |

---

## 3. 认证与令牌管理

### 3.1 令牌类型

| 类型 | 有效期 | 用途 | 存储位置 |
|------|--------|------|----------|
| Access Token | 2 小时 | API 请求认证 | 内存（TokenStore） |
| Refresh Token | 7 天 | 刷新 Access Token | 加密 JSON 文件 |
| Share Token | 1 小时 | 分享访问认证 | 不持久化 |

### 3.2 令牌存储

**存储位置:** `~/.cache/disk-ui/token.json`

**文件权限:** `0600`（仅所有者可读写）

**JSON 结构:**

```json
{
  "version": 1,
  "accessToken": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "refreshToken": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "expiresAtEpochMs": 1705363200000
}
```

### 3.3 登录流程

```
用户输入账号密码
    ↓
AuthService::Login(account, password, ctx, callback)
    ↓
[本地验证] - 检查账号密码非空
    ↓
POST /api/auth/login
    ↓
[网络响应]
    ↓
解析 LoginResultDto
    ↓
TokenStore::Save(accessToken, refreshToken, expiresIn)
    ↓
ApiClient::SetBearerToken(accessToken)
    ↓
回调通知登录成功
```

### 3.4 令牌刷新策略

TokenRefreshCoordinator 管理所有 API 调用者的令牌刷新，具有以下特性：

- **单飞行保证**：同一时间只执行一个刷新请求，其他调用者等待并共享结果
- **自动重试**：刷新成功后，原始请求回调会被重新调用（带 `refreshed=true` 标记）
- **主动刷新**：定时器在令牌过期前 5 分钟静默刷新
- **失败处理**：不可恢复错误（40110、40111）触发强制登出

#### 主动刷新（定时器）

- **检查间隔:** 60 秒
- **触发窗口:** 令牌过期前 5 分钟
- **触发条件:** `secsRemaining <= 300 && secsRemaining > 0`
- **行为:** 静默刷新，用户无感知

#### 被动刷新（40108 错误）

当 API 返回 `code: 40108`（TokenExpired）时：

1. 暂停当前请求处理
2. 触发令牌刷新（单飞行）
3. 刷新成功后重试原请求
4. 刷新失败则强制登出

#### 401xx 错误处理矩阵

| 错误码 | 类型 | 处理方式 | 用户提示 |
|--------|------|----------|----------|
| 40101 | InvalidCredentials | 聚焦密码框 | "用户名或密码错误" |
| 40102 | AccountLocked | 显示倒计时（15分钟） | "账户已锁定，请15分钟后重试" |
| 40103 | AccountDisabled | 显示警告对话框 | "账户已被禁用" |
| **40108** | **TokenExpired** | **自动刷新令牌** | **无提示（静默恢复）** |
| 40110 | RefreshTokenAlreadyUsed | 强制登出 | "登录已在其他设备使用，请重新登录" |
| 40111 | TokenRevoked | 强制登出 | "登录已被注销，请重新登录" |
| 其他 401xx | 认证错误 | 强制登出 | "登录已失效，请重新登录" |

#### 刷新失败处理（40110/40111）

以下错误表示令牌状态已无法恢复，必须重新登录：

- **40110** - RefreshTokenAlreadyUsed：刷新令牌被重复使用（可能另一设备已登录）
- **40111** - TokenRevoked：令牌被撤销（用户主动登出或管理员操作）
- **网络错误**：刷新请求网络失败

**处理流程：**
```
检测到 40110/40111 或网络错误
    ↓
TokenStore::Clear()              // 清除存储的令牌
ApiClient::SetBearerToken({})    // 清除请求头
emit forceLogout()               // 通知 UI 跳转登录页
    ↓
UI 层接收 forceLogout() 信号，跳转登录界面
```

**注意：** 40110 和 40111 属于安全类错误，触发后本地令牌立即失效，必须重新输入凭证登录。


### 3.5 登出流程

```
AuthService::Logout(accessToken, ctx, callback)
    ↓
访问令牌是否为空？
    - 是 → 直接清除本地凭证
    - 否 → POST /api/auth/logout
                ↓
              响应成功或为本地登出成功码？
                  - 是 → 清除本地凭证
                  - 否 → 返回错误
```

**本地登出成功码:** 40104, 40105, 40106, 40107, 40108, 40110, 40111

---

## 4. 端点映射表

### 4.1 端点概览

| 模块 | 端点数量 | 认证方式 | 说明 |
|------|----------|----------|------|
| Auth | 4 | None / Bearer | 注册/登录/刷新无需认证，登出需要 Bearer |
| User | 4 | Bearer Token | 用户资料、密码、存储统计 |
| File | 9 | Bearer Token | 文件列表、搜索、操作、下载（上传由专用传输引擎处理，见第7节） |
| Folder | 3 | Bearer Token | 文件夹创建、目录树、面包屑 |
| Share | 8 | Bearer / X-Share-Token / None | 管理用 Bearer，浏览/下载用 X-Share-Token，访问无需认证 |
| Trash | 4 | Bearer Token | 回收站列表、恢复、删除 |
| **总计** | **32** | - | - |

**认证方式说明：**

- **Bearer Token**: `Authorization: Bearer <access_token>`，用于绝大多数端点（28个）
- **X-Share-Token**: `X-Share-Token: <share_token>`，**不是** `Authorization: Bearer`，仅用于分享的浏览和下载（2个端点）
- **None**: 无需认证，用于注册、登录、刷新令牌、访问分享（4个端点）

**X-Share-Token 端点（关键区别）：**

| 端点 | 认证头 | 令牌来源 |
|------|--------|----------|
| GET /api/share/browse/{share_id} | `X-Share-Token: <token>` | POST /api/share/access/{share_id} 响应 |
| GET /api/share/download/{share_id}/{file_id} | `X-Share-Token: <token>` | POST /api/share/access/{share_id} 响应 |
### 4.2 完整端点列表

| 方法 | 路径 | API 类 | 方法名 | 认证 |
|------|------|--------|--------|------|
| **Auth** |||||
| POST | `/api/auth/register` | AuthApi | Register | None |
| POST | `/api/auth/login` | AuthApi | Login | None |
| POST | `/api/auth/refresh` | AuthApi | Refresh | None |
| POST | `/api/auth/logout` | AuthApi | Logout | Bearer |
| **User** |||||
| GET | `/api/user/profile` | UserApi | GetProfile | Bearer |
| PATCH | `/api/user/profile` | UserApi | UpdateProfile | Bearer |
| PUT | `/api/user/password` | UserApi | ChangePassword | Bearer |
| GET | `/api/user/storage` | UserApi | GetStorage | Bearer |
| **File** |||||
| GET | `/api/file/list` | FileApi | List | Bearer |
| GET | `/api/file/search` | FileApi | Search | Bearer |
| GET | `/api/file/{file_id}` | FileApi | GetDetail | Bearer |
| PUT | `/api/file/{file_id}/rename` | FileApi | Rename | Bearer |
| PUT | `/api/file/move` | FileApi | Move | Bearer |
| POST | `/api/file/copy` | FileApi | Copy | Bearer |
| DELETE | `/api/file` | FileApi | Delete | Bearer |
| GET | `/api/file/download/{file_id}` | FileApi | Download | Bearer |
| GET | `/api/file/download/{file_id}/info` | FileApi | DownloadInfo | Bearer |
| **Folder** |||||
| POST | `/api/folder/create` | FolderApi | CreateFolder | Bearer |
| GET | `/api/folder/tree` | FolderApi | GetTree | Bearer |
| GET | `/api/folder/{folder_id}/breadcrumb` | FolderApi | GetBreadcrumb | Bearer |
| **Share** |||||
| POST | `/api/share` | ShareApi | Create | Bearer |
| GET | `/api/share` | ShareApi | List | Bearer |
| GET | `/api/share/{share_id}` | ShareApi | GetDetail | Bearer |
| PUT | `/api/share/{share_id}` | ShareApi | Update | Bearer |
| DELETE | `/api/share` | ShareApi | Cancel | Bearer |
| POST | `/api/share/access/{share_id}` | ShareApi | Access | None |
| GET | `/api/share/browse/{share_id}` | ShareApi | Browse | X-Share-Token |
| GET | `/api/share/download/{share_id}/{file_id}` | ShareApi | Download | X-Share-Token |
| **Trash** |||||
| GET | `/api/trash` | TrashApi | List | Bearer |
| POST | `/api/trash/restore` | TrashApi | Restore | Bearer |
| DELETE | `/api/trash` | TrashApi | Delete | Bearer |
| DELETE | `/api/trash/all` | TrashApi | ClearAll | Bearer |

---

## 5. 详细端点契约

### 5.1 Auth Module

#### POST /api/auth/register

**认证:** None

**请求字段:**

| 字段 | 类型 | 必需 | 约束 | 来源 |
|------|------|------|------|------|
| `username` | `string` | 是 | 4-32 字符，字母数字下划线 | `RegisterRequest.username` |
| `email` | `string` | 是 | 有效邮箱格式 | `RegisterRequest.email` |
| `password` | `string` | 是 | 8-64 字符，字母+数字 | `RegisterRequest.password` |

**响应字段:**

| 字段 | 类型 | 来源 | 说明 |
|------|------|------|------|
| `user.id` | `quint64` | `UserDto.id` | 用户 ID |
| `user.username` | `string` | `UserDto.username` | 用户名 |
| `user.email` | `string` | `UserDto.email` | 邮箱地址 |
| `user.nickname` | `string` | `UserDto.nickname` | 显示名称 |
| `user.storage_quota` | `quint64` | `UserDto.storageQuota` | 存储配额（字节） |
| `user.storage_used` | `quint64` | `UserDto.storageUsed` | 已用存储（字节） |
| `user.created_at` | `string` | `UserDto.createdAt` | ISO 8601 时间戳 |

**错误码:** 40001, 40002, 40003, 10002

---

#### POST /api/auth/login

**认证:** None

**请求字段:**

| 字段 | 类型 | 必需 | 说明 | 来源 |
|------|------|------|------|------|
| `account` | `string` | 是 | 用户名或邮箱 | `LoginRequest.account` |
| `password` | `string` | 是 | 明文密码 | `LoginRequest.password` |

**响应字段:**

| 字段 | 类型 | 来源 | 说明 |
|------|------|------|------|
| `access_token` | `string` | `LoginResultDto.accessToken` | JWT 访问令牌（2h 有效期） |
| `refresh_token` | `string` | `LoginResultDto.refreshToken` | 单次使用刷新令牌（7d 有效期） |
| `token_type` | `string` | `LoginResultDto.tokenType` | 固定值 "Bearer" |
| `expires_in` | `int` | `LoginResultDto.expiresIn` | 访问令牌有效期（秒） |
| `user.*` | `object` | `UserDto` | 用户信息（同 register） |

**错误码:** 40101, 40102, 40103, 10005

---

#### POST /api/auth/refresh

**认证:** None（使用 refresh_token）

**请求字段:**

| 字段 | 类型 | 必需 | 说明 | 来源 |
|------|------|------|------|------|
| `refresh_token` | `string` | 是 | 单次使用刷新令牌 | `RefreshTokenRequest.refreshToken` |

**响应字段:**

| 字段 | 类型 | 来源 | 说明 |
|------|------|------|------|
| `access_token` | `string` | `RefreshResultDto.accessToken` | 新 JWT 访问令牌 |
| `refresh_token` | `string` | `RefreshResultDto.refreshToken` | 新单次使用刷新令牌 |
| `expires_in` | `int` | `RefreshResultDto.expiresIn` | 访问令牌有效期（秒） |

**错误码:** 40105, 40110, 40111

**重要:** 刷新后旧的 refresh_token 立即失效，必须使用新的令牌对。

---

#### POST /api/auth/logout

**认证:** Bearer Token

**请求字段:** None

**响应:** `data: null`

**错误码:** 40106, 40107, 40108

---

### 5.2 User Module

#### GET /api/user/profile

**认证:** Bearer Token

**请求字段:** None

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `user.id` | `quint64` | 用户 ID |
| `user.username` | `string` | 用户名 |
| `user.email` | `string` | 邮箱地址 |
| `user.nickname` | `string` | 显示名称 |
| `user.avatar` | `string?` | 头像 URL（可为 null） |
| `user.storage_used` | `quint64` | 已用存储（字节） |
| `user.storage_quota` | `quint64` | 存储配额（字节） |
| `user.file_count` | `int` | 文件总数 |
| `user.folder_count` | `int` | 文件夹总数 |
| `user.created_at` | `string` | ISO 8601 时间戳 |
| `user.updated_at` | `string` | ISO 8601 时间戳 |

**错误码:** 40106, 40107, 40108

---

#### PATCH /api/user/profile

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 约束 | 说明 |
|------|------|------|------|------|
| `nickname` | `string` | 否 | 2-32 字符 | 新显示名称 |
| `avatar` | `string` | 否 | HTTPS URL | 新头像地址 |

**响应字段:** 同 GET /api/user/profile

**错误码:** 10001, 10002, 10004, 40106-40108

---

#### PUT /api/user/password

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 约束 | 说明 |
|------|------|------|------|------|
| `old_password` | `string` | 是 | - | 当前密码 |
| `new_password` | `string` | 是 | 8-64 字符，字母+数字 | 新密码 |

**响应:** `data: null`

**错误码:** 40101（旧密码错误）, 10002, 40106-40108

---

#### GET /api/user/storage

**认证:** Bearer Token

**请求字段:** None

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `used` | `quint64` | 已用存储（字节） |
| `quota` | `quint64` | 存储配额（字节） |
| `percentage` | `double` | 使用百分比（1 位小数） |
| `categories` | `array` | 文件类型分类统计（当前为空） |

**错误码:** 40106, 40107, 40108

---

### 5.3 File Module

#### GET /api/file/list

**认证:** Bearer Token

**请求字段（Query）:**

| 字段 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `parent_id` | `int` | 否 | 0 | 父文件夹 ID，0 表示根目录 |
| `page` | `int` | 否 | 1 | 页码 |
| `page_size` | `int` | 否 | 20 | 每页数量，最大 100 |
| `sort_by` | `string` | 否 | - | 排序字段：name/size/created_at/updated_at |
| `sort_order` | `string` | 否 | asc | 排序方向：asc/desc |
| `type` | `string` | 否 | all | 过滤类型：all/file/folder |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `items[]` | `array` | 文件/文件夹列表 |
| `items[].id` | `quint64` | 项目 ID |
| `items[].name` | `string` | 项目名称 |
| `items[].type` | `string` | "file" 或 "folder" |
| `items[].size` | `quint64` | 文件大小（仅文件） |
| `items[].mime_type` | `string` | MIME 类型（仅文件） |
| `items[].hash` | `string` | MD5 哈希（仅文件） |
| `items[].item_count` | `int` | 子项数量（仅文件夹） |
| `items[].created_at` | `string` | 创建时间 |
| `items[].updated_at` | `string` | 更新时间 |
| `pagination.page` | `int` | 当前页码 |
| `pagination.page_size` | `int` | 每页数量 |
| `pagination.total` | `int` | 总数量 |
| `pagination.total_pages` | `int` | 总页数 |

**错误码:** 10001, 10002, 50006, 40106-40108

---

#### GET /api/file/search

**认证:** Bearer Token

**请求字段（Query）:**

| 字段 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `keyword` | `string` | 是 | - | 搜索关键词，1-100 字符 |
| `type` | `string` | 否 | all | 过滤类型：all/file/folder |
| `folder_id` | `int` | 否 | - | 搜索范围，null 表示全局 |
| `page` | `int` | 否 | 1 | 页码 |
| `page_size` | `int` | 否 | 20 | 每页数量 |

**响应字段:** 同 GET /api/file/list，额外包含 `items[].path`（完整路径字符串）

**错误码:** 10002, 40106-40108

---

#### GET /api/file/{file_id}

**认证:** Bearer Token

**请求字段（Path）:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `file_id` | `int` | 是 | 文件或文件夹 ID |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | `quint64` | 项目 ID |
| `name` | `string` | 项目名称 |
| `type` | `string` | "file" 或 "folder" |
| `size` | `quint64` | 文件大小（字节） |
| `hash` | `string` | MD5 哈希 |
| `mime_type` | `string` | MIME 类型 |
| `parent_id` | `quint64` | 父文件夹 ID |
| `path` | `string` | 完整路径 |
| `created_at` | `string` | 创建时间 |
| `updated_at` | `string` | 更新时间 |

**错误码:** 50005, 40106-40108

---

#### PUT /api/file/{file_id}/rename

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `file_id` | `int` | 是 | Path 参数，文件/文件夹 ID |
| `new_name` | `string` | 是 | Body 参数，新名称 |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | `quint64` | 项目 ID |
| `name` | `string` | 新名称 |
| `updated_at` | `string` | 更新时间 |

**错误码:** 50001, 50005, 50007, 40106-40108

---

#### PUT /api/file/move

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `file_ids` | `array[int]` | 是 | 要移动的文件/文件夹 ID 列表 |
| `target_folder_id` | `int` | 是 | 目标文件夹 ID（0 = 根目录） |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `moved_count` | `int` | 成功移动的项目数量 |

**错误码:** 50005, 50006, 10001, 10002, 40106-40108

---

#### POST /api/file/copy

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `file_ids` | `array[int]` | 是 | 要复制的文件/文件夹 ID 列表 |
| `target_folder_id` | `int` | 是 | 目标文件夹 ID（0 = 根目录） |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `copied_count` | `int` | 成功复制的项目数量 |
| `new_files[]` | `array` | ID 映射列表 |
| `new_files[].old_id` | `quint64` | 原始 ID |
| `new_files[].new_id` | `quint64` | 新 ID |

**错误码:** 50004, 50005, 50006, 40106-40108

---

#### DELETE /api/file

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `file_ids` | `array[int]` | 是 | 要删除的文件/文件夹 ID 列表 |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `deleted_count` | `int` | 移入回收站的项目数量 |

**错误码:** 50005, 10001, 10002, 40106-40108

**注意:** 这是软删除操作，文件移入回收站，存储空间不释放。

---

#### GET /api/file/download/{file_id}

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 位置 | 说明 |
|------|------|------|------|------|
| `file_id` | `int` | 是 | Path | 文件 ID |
| `Range` | `string` | 否 | Header | 字节范围，如 "bytes=0-1023" |

**响应:** 二进制文件流（200 OK 或 206 Partial Content）

**响应头:** `Content-Type`, `Content-Length`, `Content-Disposition`, `Accept-Ranges`, `Content-Range`（206 时）

**错误码:** 50005, 416, 40106-40108

---

#### GET /api/file/download/{file_id}/info

**认证:** Bearer Token

**请求字段（Path）:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `file_id` | `int` | 是 | 文件 ID |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `file_id` | `quint64` | 文件 ID |
| `filename` | `string` | 文件名 |
| `file_size` | `quint64` | 文件大小（字节） |
| `file_hash` | `string` | MD5 哈希 |
| `mime_type` | `string` | MIME 类型 |
| `supports_range` | `bool` | 是否支持断点续传 |

**错误码:** 50005, 40106-40108

**注意:** ⚠️ 此端点在当前后端 API 文档中未找到定义，但 QML 客户端代码中存在。可能是未文档化的端点或实现方式不同。

---

### 5.4 Folder Module

#### POST /api/folder/create

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `name` | `string` | 是 | - | 文件夹名称，1-255 字符 |
| `parent_id` | `int` | 否 | 0 | 父文件夹 ID，0 表示根目录 |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | `quint64` | 新文件夹 ID |
| `name` | `string` | 文件夹名称 |
| `parent_id` | `quint64` | 父文件夹 ID |
| `path` | `string` | 完整路径 |
| `created_at` | `string` | 创建时间 |

**错误码:** 50001, 50006, 50010, 10002, 40106-40108

---

#### GET /api/folder/tree

**认证:** Bearer Token

**请求字段（Query）:**

| 字段 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `parent_id` | `int` | 否 | 0 | 起始文件夹 ID |
| `depth` | `int` | 否 | -1 | 展开深度，-1 表示全部 |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `tree[]` | `array` | 根节点列表 |
| `tree[].id` | `quint64` | 文件夹 ID |
| `tree[].name` | `string` | 文件夹名称 |
| `tree[].children[]` | `array` | 子文件夹（递归） |

**错误码:** 40106-40108

---

#### GET /api/folder/{folder_id}/breadcrumb

**认证:** Bearer Token

**请求字段（Path）:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `folder_id` | `int` | 是 | 文件夹 ID |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `path[]` | `array` | 面包屑项列表 |
| `path[].id` | `quint64` | 文件夹 ID |
| `path[].name` | `string` | 文件夹名称 |

**错误码:** 50006, 10001, 40106-40108

---

### 5.5 Share Module

#### POST /api/share

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `file_ids` | `array[int]` | 是 | - | 要分享的文件/文件夹 ID 列表 |
| `expire_days` | `int` | 否 | 7 | 过期天数，0 表示永久 |
| `password` | `string` | 否 | - | 访问密码，4-8 字符 |
| `permission` | `string` | 否 | download | 权限：view/download |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `share_id` | `string` | 分享 ID（如 "sh_abc123"） |
| `share_link` | `string` | 完整分享 URL |
| `password` | `string?` | 密码（如果设置） |
| `permission` | `string` | 权限级别 |
| `expires_at` | `string` | 过期时间 |
| `created_at` | `string` | 创建时间 |

**错误码:** 10001, 10002, 50005, 40106-40108

---

#### GET /api/share

**认证:** Bearer Token

**请求字段（Query）:**

| 字段 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `status` | `string` | 否 | all | 状态：all/active/expired/cancelled |
| `page` | `int` | 否 | 1 | 页码 |
| `page_size` | `int` | 否 | 20 | 每页数量 |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `items[]` | `array` | 分享列表 |
| `items[].share_id` | `string` | 分享 ID |
| `items[].file_name` | `string` | 主文件名称 |
| `items[].file_count` | `int` | 文件数量 |
| `items[].share_link` | `string` | 分享链接 |
| `items[].has_password` | `bool` | 是否有密码保护 |
| `items[].permission` | `string` | 权限级别 |
| `items[].view_count` | `int` | 查看次数 |
| `items[].download_count` | `int` | 下载次数 |
| `items[].created_at` | `string` | 创建时间 |
| `items[].expires_at` | `string` | 过期时间 |
| `items[].status` | `string` | active/expired/cancelled |
| `pagination.*` | `object` | 分页信息 |

**错误码:** 10001, 40106-40108

---

#### GET /api/share/{share_id}

**认证:** Bearer Token

**请求字段（Path）:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `share_id` | `string` | 是 | 分享 ID |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `share_id` | `string` | 分享 ID |
| `files[]` | `array` | 分享的文件列表 |
| `files[].id` | `quint64` | 文件 ID |
| `files[].name` | `string` | 文件名称 |
| `files[].type` | `string` | "file" 或 "folder" |
| `files[].size` | `quint64` | 文件大小 |
| `share_link` | `string` | 分享链接 |
| `has_password` | `bool` | 是否有密码 |
| `permission` | `string` | 权限级别 |
| `view_count` | `int` | 查看次数 |
| `download_count` | `int` | 下载次数 |
| `created_at` | `string` | 创建时间 |
| `expires_at` | `string` | 过期时间 |
| `status` | `string` | 状态 |

**错误码:** 60001, 10001, 40106-40108

---

#### PUT /api/share/{share_id}

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `share_id` | `string` | 是 | Path 参数，分享 ID |
| `expire_days` | `int` | 否 | 新过期天数（从当前时间计算） |
| `password` | `string` | 否 | 新密码，"" 表示移除密码 |
| `permission` | `string` | 否 | 新权限 |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `share_id` | `string` | 分享 ID |
| `expires_at` | `string` | 新过期时间 |
| `has_password` | `bool` | 是否设置了密码 |
| `permission` | `string` | 权限级别 |
| `updated_at` | `string` | 更新时间 |

**错误码:** 60001, 10002, 40106-40108

---

#### DELETE /api/share

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `share_ids` | `array[string]` | 是 | 要取消的分享 ID 列表 |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `summary.total` | `int` | 请求总数 |
| `summary.succeeded` | `int` | 成功取消数 |
| `summary.failed` | `int` | 失败数 |
| `results[]` | `array` | 每项结果 |
| `results[].share_id` | `string` | 分享 ID |
| `results[].status` | `string` | "success" 或 "failed" |
| `results[].error` | `object?` | 错误详情（失败时） |

**错误码:** 10001, 40106-40108

**注意:** 始终返回 HTTP 200，结果状态在响应体中。

---

#### POST /api/share/access/{share_id}

**认证:** None（公开访问）

**请求字段:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `share_id` | `string` | 是 | Path 参数，分享 ID |
| `password` | `string` | 否 | 访问密码（如需要） |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `share_token` | `string` | JWT 分享令牌（1h 有效期） |
| `expires_in` | `int` | 令牌有效期（秒） |
| `permission` | `string` | 权限级别 |
| `files[]` | `array` | 分享的文件列表 |

**错误码:** 60001, 60002, 60003, 10001

---

#### GET /api/share/browse/{share_id}

**认证:** X-Share-Token（非 Bearer Token）

**请求字段:**

| 字段 | 类型 | 必需 | 位置 | 说明 |
|------|------|------|------|------|
| `share_id` | `string` | 是 | Path | 分享 ID |
| `folder_id` | `int` | 否 | Query | 分享内的文件夹 ID |
| `X-Share-Token` | `string` | 是 | Header | 从 /share/access 获取的令牌 |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `items[]` | `array` | 浏览的项目列表 |
| `items[].id` | `quint64` | 项目 ID |
| `items[].name` | `string` | 项目名称 |
| `items[].type` | `string` | "file" 或 "folder" |
| `items[].size` | `quint64` | 文件大小 |
| `breadcrumb[]` | `array` | 路径面包屑 |

**错误码:** 60001, 60002, 50006, 40106-40108

**注意:** 使用 `X-Share-Token` 请求头，而非 `Authorization: Bearer`。

---

#### GET /api/share/download/{share_id}/{file_id}

**认证:** X-Share-Token（非 Bearer Token）

**请求字段:**

| 字段 | 类型 | 必需 | 位置 | 说明 |
|------|------|------|------|------|
| `share_id` | `string` | 是 | Path | 分享 ID |
| `file_id` | `int` | 是 | Path | 分享内的文件 ID |
| `X-Share-Token` | `string` | 是 | Header | 分享令牌 |
| `Range` | `string` | 否 | Header | 字节范围 |

**响应:** 二进制文件流

**错误码:** 60001, 60002, 60004（仅查看权限）, 50005, 40106-40108

---

### 5.6 Trash Module

#### GET /api/trash

**认证:** Bearer Token

**请求字段（Query）:**

| 字段 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `page` | `int` | 否 | 1 | 页码 |
| `page_size` | `int` | 否 | 20 | 每页数量 |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `items[]` | `array` | 回收站项目列表 |
| `items[].id` | `quint64` | 回收站项目 ID |
| `items[].type` | `string` | "file" 或 "folder" |
| `items[].original_id` | `quint64` | 原始文件/文件夹 ID |
| `items[].name` | `string` | 项目名称 |
| `items[].size` | `quint64` | 文件大小 |
| `items[].original_path` | `string` | 原始路径 |
| `items[].deleted_at` | `string` | 删除时间 |
| `items[].expires_at` | `string` | 自动删除时间 |
| `pagination.*` | `object` | 分页信息 |

**错误码:** 10001, 40106-40108

---

#### POST /api/trash/restore

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `trash_ids` | `array[int]` | 是 | 要恢复的回收站项目 ID 列表 |

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `summary.total` | `int` | 请求总数 |
| `summary.success_count` | `int` | 成功恢复数 |
| `summary.failure_count` | `int` | 失败数 |
| `results[]` | `array` | 每项结果 |
| `results[].trash_id` | `quint64` | 回收站项目 ID |
| `results[].success` | `bool` | 是否成功 |
| `results[].message` | `string` | 状态消息 |

**错误码:** 10001, 40106-40108

**注意:** 如果原父文件夹不存在，项目恢复到根目录。命名冲突自动重命名。

---

#### DELETE /api/trash

**认证:** Bearer Token

**请求字段:**

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `trash_ids` | `array[int]` | 是 | 要彻底删除的回收站项目 ID 列表 |

**响应字段:** 同 POST /api/trash/restore

**错误码:** 10001, 10003, 40106-40108

**注意:** 此操作永久删除文件并释放存储空间。

---

#### DELETE /api/trash/all

**认证:** Bearer Token

**请求字段:** None

**响应字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `deleted_count` | `int` | 删除的项目数量 |
| `freed_space` | `quint64` | 释放的存储空间（字节） |

**错误码:** 40106-40108

---

## 6. 字段类型映射

### 6.1 C++ 与 JSON 类型对照

| C++ 类型 | JSON 类型 | 说明 |
|----------|-----------|------|
| `quint64` | `number` | 大整数通过 `toDouble()` 解析 |
| `int` | `number` | 直接映射 |
| `bool` | `boolean` | 直接映射 |
| `QString` | `string` | UTF-8 编码 |
| `QVector<T>` | `array` | 顺序容器 |
| `std::optional<T>` | `T \| null` | 可选/可空字段 |

### 6.2 日期时间格式

所有时间戳使用 ISO 8601 格式字符串，例如：`"2024-01-15T08:30:00Z"`

---

## 7. 范围外端点

以下后端端点**未在 QML 客户端中实现**：

| 端点 | 说明 | 排除原因 |
|------|------|----------|
| `POST /api/file/upload/init` | 初始化上传 | QML 客户端使用独立的上传/下载引擎处理文件传输，不在标准 REST API 客户端中 |
| `GET /api/health` | 健康检查 | 运维监控端点，桌面客户端不需要 |
| `GET /api/system/info` | 系统信息 | 管理功能，与终端用户文件管理无关 |

### 7.1 上传流程说明

虽然 `POST /api/file/upload/init` 不在 QML API 客户端范围内，但完整的文件上传功能通过专用的传输管理器实现：

- 分片上传、秒传检测、断点续传由专用 C++ 传输引擎处理
- 标准的 `FileApi` 类仅提供下载功能（`Download`, `DownloadInfo`）
- 上传功能通过独立的文件传输管理器暴露给 QML 层

---

## 8. 实现注意事项

### 8.1 Share Token 认证

分享相关的浏览和下载端点使用 `X-Share-Token` 请求头，**不是** `Authorization: Bearer`。这是 QML 客户端实现的关键细节：

```cpp
// 正确
request.setRawHeader("X-Share-Token", shareToken.toUtf8());

// 错误
request.setRawHeader("Authorization", "Bearer " + shareToken.toUtf8());
```

### 8.2 批量操作结果处理

分享取消、回收站恢复/删除使用混合结果模式：

- 始终返回 HTTP 200
- 实际结果在响应体的 `summary` 和 `results` 中
- 需要逐项检查 `status` 或 `success` 字段

### 8.3 软删除与硬删除

- `DELETE /api/file` - 软删除，移入回收站，不释放空间
- `DELETE /api/trash` - 硬删除，永久删除，释放空间

### 8.4 分页一致性

所有列表端点使用统一的分页结构：

```json
{
  "pagination": {
    "page": 1,
    "page_size": 20,
    "total": 100,
    "total_pages": 5
  }
}
```

### 8.5 混合列表类型

文件列表和搜索结果返回混合的文件和文件夹，通过 `type` 字段区分（"file" 或 "folder"）。

---

## 9. 文档版本历史

| 版本 | 日期 | 变更说明 |
|------|------|----------|
| 1.0 | 2026-03-08 | 初始版本，整合 Wave 1 任务输出 |
| 1.1 | 2026-03-13 | 同步架构文档，验证 endpoint checker 差异为 0 |
---

## 10. 参考文档

- [后端 API 设计](../../design/02-API接口设计.md) - 后端完整 API 规范
- [QML 架构设计](./03-架构设计.md) - QML 客户端整体架构
