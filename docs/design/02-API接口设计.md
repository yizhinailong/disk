# 网盘系统 - API 接口设计

## 1. 接口规范

### 1.1 基础信息

| 项目 | 说明 |
|------|------|
| 基础路径 | `/api` |
| 协议 | HTTPS |
| 数据格式 | JSON |
| 字符编码 | UTF-8 |
| 时间格式 | ISO 8601（如 `2026-01-13T10:30:00Z`） |

### 1.2 请求头

| Header | 必填 | 说明 |
|--------|------|------|
| Content-Type | 是 | `application/json`（分片上传时为 `application/octet-stream`，详见上传分片接口） |
| Authorization | 是* | `Bearer <access_token>`（需认证的接口） |
| X-Request-ID | 否 | 1-128 个 `[A-Za-z0-9._:-]` 字符的请求追踪 ID；合法值由服务端原样用于日志关联，缺失或非法时由服务端通过系统级密码学随机源生成 UUID v4 |

所有 HTTP 响应（包括认证、校验和业务失败）返回 `X-Request-Id` 与
`X-Disk-Instance-Id`。前者等于本次请求采用的追踪 ID，后者是实际处理请求的进程实例 ID；调用方必须把二者与业务资源 ID 一并保留用于故障定位。客户端提供的追踪 ID 只作为日志关联值，服务端会在写日志前校验长度和字符集，非法值不会回显或写入日志，也不会导致业务请求失败。

### 1.3 响应格式

#### 成功响应

```json
{
  "code": 0,
  "message": "success",
  "data": {
    // 业务数据
  }
}
```

#### 错误响应

```json
{
  "code": 40001,
  "message": "用户名已被注册",
  "data": null
}
```

#### 分页响应

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "items": [],
    "pagination": {
      "page": 1,
      "page_size": 20,
      "total": 100,
      "total_pages": 5
    }
  }
}
```

### 1.4 错误码定义

错误码枚举定义位于 `src/utils/ErrorCode.hpp`，命名空间为 `disk::error::Code`。

#### 通用错误码

| 错误码 | 枚举名称 | HTTP状态码 | 说明 |
|--------|----------|------------|------|
| 0 | `Success` | 200 | 成功 |
| 10001 | `InvalidParameter` | 400 | 请求参数错误 |
| 10002 | `ValidationFailed` | 400 | 参数校验失败 |
| 10003 | `ResourceNotFound` | 404 | 资源不存在 |
| 10004 | `ResourceConflict` | 409 | 资源冲突 |
| 10005 | `TooManyRequests` | 429 | 请求过于频繁 |
| 10006 | `InternalError` | 500 | 服务器内部错误 |

#### 认证错误码

| 错误码 | 枚举名称 | HTTP状态码 | 说明 |
|--------|----------|------------|------|
| 40001 | `UsernameExists` | 400 | 用户名已存在 |
| 40002 | `EmailExists` | 400 | 邮箱已存在 |
| 40101 | `InvalidCredentials` | 401 | 用户名或密码错误 |
| 40102 | `AccountLocked` | 401 | 账户已锁定 |
| 40103 | `AccountDisabled` | 401 | 账户已禁用 |
| 40104 | `InvalidToken` | 401 | 令牌无效或已过期 |
| 40105 | `InvalidRefreshToken` | 401 | 刷新令牌无效 |
| 40106 | `TokenMissing` | 401 | 未提供令牌 |
| 40107 | `TokenMalformed` | 401 | 令牌格式错误 |
| 40108 | `TokenExpired` | 401 | 令牌已过期 |
| 40100 | `UserNotFound` | 404 | 用户不存在 |
| 40109 | `TokenWrongType` | 401 | 令牌类型错误 |
| 40110 | `RefreshTokenAlreadyUsed` | 401 | 刷新令牌已被使用 |
| 40111 | `TokenRevoked` | 401 | 令牌已被注销 |

#### 文件错误码

| 错误码 | 枚举名称 | HTTP状态码 | 说明 |
|--------|----------|------------|------|
| 50001 | `InvalidFilename` | 400 | 文件名无效 |
| 50004 | `StorageQuotaExceeded` | 400 | 存储空间不足 |
| 50005 | `FileNotFound` | 404 | 文件不存在 |
| 50006 | `FolderNotFound` | 404 | 文件夹不存在 |
| 50007 | `FileAlreadyExists` | 409 | 同名文件已存在 |
| 50008 | `UploadTaskNotFound` | 400 | 上传任务不存在或已过期 |
| 50009 | `ChunkVerifyFailed` | 400 | 分片校验失败 |
| 50010 | `FolderAlreadyExists` | 409 | 同名文件夹已存在 |
| 50011 | `FileReadError` | 500 | 文件读取失败 |
| 50012 | `UploadTaskCreationDisabled` | 503 | 新上传任务创建已临时关闭 |
| 50013 | `UploadLifecycleFrozen` | 503 | 上传生命周期已为回滚临时冻结 |

#### 分享错误码

| 错误码 | 枚举名称 | HTTP状态码 | 说明 |
|--------|----------|------------|------|
| 60001 | `ShareNotFound` | 404 | 分享不存在 |
| 60002 | `ShareExpired` | 400 | 分享已过期 |
| 60003 | `SharePasswordError` | 400 | 分享密码错误 |
| 60004 | `ShareAccessDenied` | 403 | 无权限访问 |

#### Redis错误码

| 错误码 | 枚举名称 | HTTP状态码 | 说明 |
|--------|----------|------------|------|
| 70002 | `RedisOperationFailed` | 500 | Redis操作失败 |
| 70003 | `RedisKeyNotFound` | 404 | Redis key不存在 |

#### 管理员错误码

| 错误码 | 枚举名称 | HTTP状态码 | 说明 |
|--------|----------|------------|------|
| 80001 | `AdminRequired` | 403 | 需要管理员权限 |
| 80002 | `AdminUserNotFound` | 404 | 用户不存在 |
| 80003 | `AdminCannotModifySelf` | 400 | 不能修改自己的状态或角色 |
| 80004 | `AdminCannotDemoteLast` | 400 | 不能降级最后一个管理员 |
| 80005 | `AdminShareNotFound` | 404 | 分享不存在 |
| 80006 | `AdminInvalidStatus` | 400 | 无效的用户状态 |
| 80007 | `AdminInvalidRole` | 400 | 无效的角色 |

#### 代码使用示例

```cpp
#include "utils/ErrorCode.hpp"

[[nodiscard]]
auto GetUser(int id) -> Result<User> {
    if (id <= 0) {
        return std::unexpected(ErrorInfo(ErrorCode::InvalidParameter));
    }
    // ...
    return user;
}
```

---

## 2. 认证接口

### 2.1 用户注册

**POST** `/api/auth/register`

注册新用户账户。

#### 请求参数

```json
{
  "username": "john_doe",
  "email": "john@example.com",
  "password": "SecurePass123"
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| username | string | 是 | 用户名，4-32字符，字母数字下划线 |
| email | string | 是 | 邮箱地址 |
| password | string | 是 | 密码，8-64字符，仅含大小写字母和数字（不支持特殊字符） |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "user": {
      "id": 1,
      "username": "john_doe",
      "email": "john@example.com",
      "nickname": "john_doe",
      "storage_quota": 10737418240,
      "storage_used": 0,
      "created_at": "2026-01-13T10:00:00Z"
    }
  }
}
```

---

### 2.2 用户登录

**POST** `/api/auth/login`

用户身份验证，获取访问令牌。

#### 请求参数

```json
{
  "account": "john_doe",
  "password": "SecurePass123"
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| account | string | 是 | 用户名或邮箱 |
| password | string | 是 | 密码 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "access_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
    "refresh_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
    "token_type": "Bearer",
    "expires_in": 7200,
    "user": {
      "id": 1,
      "username": "john_doe",
      "email": "john@example.com",
      "nickname": "john_doe",
      "avatar": null,
      "storage_used": 1073741824,
      "storage_quota": 10737418240
    }
  }
}
```

---

#### 账户保护语义

- 同一账户的连续密码失败次数以 PostgreSQL 原子更新累计；第 5 次失败把 `locked_until` 设为数据库 `NOW() + 15 minutes`，不修改管理员控制的 `status`。并发请求命中不同 API 实例时仍只能按数据库行顺序累计，不能因 ORM 先读后写丢失增量或延长已有锁定。
- `status=0` 表示管理员禁用，`status=2 + locked_until=NULL` 表示管理员锁定；两者只有管理员状态变更可以解除。`status=1 + locked_until>NOW()` 表示密码失败触发的临时锁定，登录和 refresh 均返回 `401 + 40102 AccountLocked`。
- 临时锁定是否到期只使用 PostgreSQL `NOW()`。到期后的下一次正确登录在签发令牌前原子清零 `login_attempts/locked_until`；兼容旧实现产生的 `status=2 + 已过期 locked_until` 行时，同时恢复为 `status=1`。管理员修改状态会清空密码失败计数和临时截止时间，使人工状态与自动锁定保持可区分。

---

### 2.3 刷新令牌

**POST** `/api/auth/refresh`

使用刷新令牌获取新的访问令牌。

#### 请求参数

```json
{
  "refresh_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "access_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
    "refresh_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
    "expires_in": 7200
  }
}
```

---

### 2.4 用户登出

**POST** `/api/auth/logout`

使当前令牌失效。撤销状态成功写入共享 Redis 后，服务端才返回登出成功；各 API 实例不缓存“未撤销”结果，因此同一 JTI 的下一次受保护请求必须立即返回 `40111` / HTTP 401。Redis 撤销校验不可用时采用 fail closed，返回 `70002` / HTTP 500，且请求不得进入业务处理。

#### 请求头

```
Authorization: Bearer <access_token>
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": null
}
```

---

## 3. 用户接口

### 实现状态说明

本章节中各接口的实现状态说明如下：

- **已实现**: 后端已注册 method+path 路由，可正常调用
- **未实现**: 文档中已定义接口契约，但后端尚未注册 method+path 路由

### 用户接口错误响应（统一）

用户接口统一使用全局错误响应格式，不在每个接口中重复展开错误 JSON：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

常见错误码参考：

- `40106` TokenMissing（未提供令牌）
- `40107` TokenMalformed（令牌格式错误）
- `40108` TokenExpired（令牌已过期）
- `10002` ValidationFailed（参数校验失败）
- `40101` InvalidCredentials（凭证错误，例如旧密码错误）

### 3.1 获取当前用户信息

**GET** `/api/user/profile`

#### 实现状态
**已实现**

获取当前登录用户的详细信息。

#### 请求头

```
Authorization: Bearer <access_token>
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "user": {
        "id": 1,
        "username": "john_doe",
        "email": "john@example.com",
        "nickname": "John",
        "avatar": "https://example.com/avatar/1.jpg",
        "storage_used": 1073741824,
        "storage_quota": 10737418240,
        "file_count": 150,
        "folder_count": 20,
        "created_at": "2026-01-01T00:00:00Z",
        "updated_at": "2026-01-10T12:00:00Z"
    }
  }
}
```

---

### 3.2 更新用户信息

**PATCH** `/api/user/profile`

#### 实现状态
**已实现**

更新当前用户的个人信息。采用 Merge Patch 风格的局部更新语义：
- 仅支持更新 `nickname` 和 `avatar` 两个字段
- 未传递的字段保持不变（不使用 `null` 清空字段）
- 空请求体或无可更新字段返回 `400 + 10001` 错误

#### 请求头

```
Authorization: Bearer <access_token>
If-Match: <etag>（可选）
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |
| If-Match | 否 | 乐观锁控制，值为用户当前 ETag |

#### 请求参数

```json
{
  "nickname": "Johnny",
  "avatar": "https://example.com/avatar/new.jpg"
}
```

 | 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| nickname | string | 否 | 昵称，1-64字符 |
| avatar | string | 否 | 头像 URL，必须符合安全约束 |

**更新规则**：
- 所有参数均为可选，至少传递一个有效字段
- 未传递的字段保持原值不变
- `username` 和 `email` 不可修改

#### 字段语义

| 场景 | 请求体示例 | 行为 |
|------|-----------|------|
| **缺失字段** | `{"nickname": "新昵称"}` | 仅更新 nickname，avatar 保持原值 |
| **显式 null** | `{"nickname": null}` | 返回 `400 + 10002` 校验失败错误 |
| **完整更新** | `{"nickname": "新昵称", "avatar": "https://..."}` | 同时更新两个字段 |

**注意**：未传递字段与显式 `null` 的语义完全不同。未传递字段表示"不修改"，显式 `null` 表示"请求校验失败"。

#### 并发控制

本接口支持可选的乐观锁机制，通过 `If-Match` 头实现：

| 场景 | Header | 行为 | HTTP 状态码 | 业务码 |
|------|--------|------|------------|--------|
| **未提供 If-Match** | - | 采用阶段性 last-write-wins 策略，直接更新 | 200 | 0 |
| **If-Match 匹配** | `If-Match: "12345"` | 版本一致，执行更新 | 200 | 0 |
| **If-Match 不匹配** | `If-Match: "99999"` | 版本冲突，拒绝更新 | 412 | 10004 |

**If-Match 不匹配响应示例**：

```json
{
  "code": 10004,
  "message": "资源冲突",
  "data": {
    "current_etag": "12346",
    "message": "用户信息已被其他请求修改，请刷新后重试"
  }
}
```

**建议**：高并发场景下，客户端应先调用 `GET /api/user/profile` 获取用户信息和 `ETag` 响应头，然后在 PATCH 请求中携带 `If-Match`。在当前阶段，若客户端不提供 `If-Match`，服务端采用 last-write-wins 策略。

#### Avatar 安全约束

`avatar` 字段必须严格遵循以下安全规则，防止 SSRF（服务器端请求伪造）攻击：

| 约束项 | 规则 | 违反后果 |
|--------|------|----------|
| **协议** | 必须使用 `https://` 协议 | 返回 `400 + 10002` 校验失败 |
| **内网地址** | 禁止以下地址段：<br>- `127.0.0.0/8`（loopback）<br>- `169.254.0.0/16`（link-local）<br>- `10.0.0.0/8`（private）<br>- `172.16.0.0/12`（private）<br>- `192.168.0.0/16`（private） | 返回 `400 + 10002` 校验失败 |
| **域名白名单**（建议） | 建议配置可信任的域名白名单，如：<br>- `cdn.example.com`<br>- `assets.example.com`<br>- `avatars.gravatar.com` | 不在白名单内的域名返回 `400 + 10002` |
| **URL 格式** | 必须为完整 URL，不接受相对路径 | 返回 `400 + 10001` 参数错误 |

**有效 avatar 示例**：
- ✅ `https://cdn.example.com/avatars/123.jpg`
- ✅ `https://avatars.gravatar.com/user/avatar`
- ✅ `https://assets.example.com/u/profile.png`

**无效 avatar 示例**：
- ❌ `http://cdn.example.com/avatar.jpg`（非 https）
- ❌ `https://127.0.0.1/avatar.jpg`（loopback）
- ❌ `https://192.168.1.10/avatar.jpg`（内网地址）
- ❌ `https://169.254.1.1/avatar.jpg`（link-local）
- ❌ `./avatar.jpg`（相对路径）

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 空请求体、无可更新字段、URL 格式错误 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 昵称长度错误、avatar 安全约束违反、显式 `null` 字段 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 `Authorization` |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | `Authorization` 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 412 | 10004 | `ResourceConflict` | 资源冲突 | `If-Match` 值与当前 ETag 不匹配 |

**10001 InvalidParameter 响应示例**：

```json
{
  "code": 10001,
  "message": "请求参数错误",
  "data": {
    "field": "avatar",
    "reason": "URL 格式错误"
  }
}
```

**10002 ValidationFailed 响应示例（avatar 安全约束违反）**：

```json
{
  "code": 10002,
  "message": "参数校验失败",
  "data": {
    "field": "avatar",
    "reason": "头像 URL 必须使用 https 协议且禁止访问内网地址",
    "invalid_value": "https://192.168.1.10/avatar.jpg"
  }
}
```

**10002 ValidationFailed 响应示例（显式 null）**：

```json
{
  "code": 10002,
  "message": "参数校验失败",
  "data": {
    "field": "nickname",
    "reason": "不支持显式设置为 null，未传递该字段即可保持原值"
  }
}
```

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

**40107 TokenMalformed 响应示例**：

```json
{
  "code": 40107,
  "message": "令牌格式错误",
  "data": {
    "reason": "Authorization 头格式应为 'Bearer <token>'"
  }
}
```

**40108 TokenExpired 响应示例**：

```json
{
  "code": 40108,
  "message": "令牌已过期",
  "data": {
    "token_type": "access_token",
    "expired_at": "2026-01-13T12:00:00Z"
  }
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "user": {
      "id": 1,
      "username": "john_doe",
      "email": "john@example.com",
      "nickname": "Johnny",
      "avatar": "https://example.com/avatar/new.jpg",
      "storage_used": 1073741824,
      "storage_quota": 10737418240,
      "file_count": 150,
      "folder_count": 20,
      "created_at": "2026-01-01T00:00:00Z",
      "updated_at": "2026-01-13T10:30:00Z"
    }
  }
}
```

---

### 3.3 修改密码

**PUT** `/api/user/password`

#### 实现状态
**已实现**

修改当前用户的登录密码。

#### 请求头

```
Authorization: Bearer <access_token>
```

#### 请求参数

```json
{
  "old_password": "OldSecurePass123",
  "new_password": "NewSecurePass456"
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| old_password | string | 是 | 当前密码 |
| new_password | string | 是 | 新密码，8-64字符 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": null
}
```

---

### 3.4 获取存储空间统计

**GET** `/api/user/storage`

#### 实现状态
**✅ 已实现**

获取用户存储空间使用详情。

#### 实现说明

- `storage_used` 使用实时 SQL 计算（SUM of Files.size）
- `storage_reserved` 使用实时 SQL 计算（SUM of upload_tasks.file_size WHERE status = 0）
- 回收站（Trash）中的文件计入 `storage_used`
- `categories` 当前版本返回空数组，后续版本支持文件类型分类
- 百分比精度为1位小数

#### 存储配额语义说明

本接口返回三个关键字段，分别代表不同的存储配额语义：

| 字段 | 计算方式 | 说明 |
|------|---------|------|
| `used` | `storage_used` | 实际已使用的存储空间，包含正常文件和回收站中的文件 |
| `reserved` | `storage_reserved` | 已预留但未完成的上传空间，防止并发上传超过配额 |
| `quota` | `storage_quota` | 用户总存储配额 |

**有效可用空间计算**：
```
有效可用 = quota - used - reserved
```

**上传生命周期配额变化**：
```
init:    storage_reserved += file_size  (预占用)
chunk:   无变化
complete: storage_reserved -= file_size, storage_used += file_size  (预占用 → 实际使用)
cancel:   storage_reserved -= file_size  (释放预占用)
expire:   storage_reserved -= file_size  (过期释放，由定时清理服务执行)
```

**重要说明**：
- 回收站文件仍计入 `storage_used`，只有永久删除（从回收站删除）才会释放
- 秒传（文件哈希匹配）不经过预占用流程，直接创建 `files` 记录，不消耗 `storage_reserved`
- 并发上传场景下，`storage_reserved` 防止多个上传任务同时超过配额

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

**40108 TokenExpired 响应示例**：

```json
{
  "code": 40108,
  "message": "令牌已过期",
  "data": {
    "token_type": "access_token",
    "expired_at": "2026-01-13T12:00:00Z"
  }
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "used": 1073741824,
    "reserved": 524288000,
    "quota": 10737418240,
    "percentage": 15.0,
    "categories": [
      {"type": "document", "size": 214748364, "count": 50},
      {"type": "image", "size": 429496729, "count": 200},
      {"type": "video", "size": 322122547, "count": 5},
      {"type": "audio", "size": 53687091, "count": 30},
      {"type": "other", "size": 53687093, "count": 15}
    ]
  }
}
```

---

## 4. 文件接口

上传生命周期的 init、chunk、complete、cancel 四个路由均要求 owner JWT，并各自只经过一次路由级
上传限流。init 创建的 `upload_id` 不构成授权凭据；后续操作必须在任何对象存储读写、组装、租约、
状态或配额变更之前，以当前认证用户和 `upload_id` 联合查询任务。缓存命中也必须验证 `user_id`，
不得绕过数据库所有权边界。不存在、已过期或属于其他用户的会话统一返回
`400 + 50008 UploadTaskNotFound`，不得泄漏会话是否存在，也不得产生 staging、分片、文件、租约或
配额副作用。同用户的完成/取消重放遵循各接口幂等语义；更换身份后即使持有相同 `upload_id`，也不
继承任何重放权限。

### 4.1 初始化上传

**POST** `/api/file/upload/init`

#### 实现状态
**已实现**

初始化文件上传任务，检测秒传和断点续传。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 请求参数

```json
{
  "filename": "document.pdf",
  "file_size": 104857600,
  "file_hash": "d41d8cd98f00b204e9800998ecf8427e",
  "parent_id": 0
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| filename | string | 是 | 文件名，1-255 字符，必须是合法 UTF-8；禁止 `/ \ : * ? " < > |`、控制字符、`.`、`..` 和以 `.` 开头 |
| file_size | integer | 是 | 文件大小（字节） |
| file_hash | string | 是 | 文件 MD5 哈希 |
| parent_id | integer | 否 | 父文件夹 ID，默认 0（根目录） |

> **📤 上传预占用语义（CRITICAL）**：本接口执行**存储空间预占用**操作：
> - `storage_reserved` 增加 `file_size`，防止并发上传超过配额
> - 如果 `storage_used + storage_reserved + file_size > storage_quota`，返回 `400 + 50004 StorageQuotaExceeded`
> - 秒传（文件哈希已存在）不经过预占用流程，直接创建 `files` 记录
> - 取消上传或超时后，预占用的空间通过 `storage_reserved` 释放
> - 同一用户并发初始化相同 `file_hash` 时，服务端以 PostgreSQL 事务级 advisory lock 串行化“活跃任务复查、配额预留、任务插入”；所有成功请求返回同一 `upload_id`，只允许一个任务和一次预留。`InProgress` 与 `Finalizing` 都属于可复用的活跃会话。
>
> **回滚截止语义**：当启动期配置 `upload_task_creation_enabled=false` 时，本接口仍先解析秒传和同用户同 hash 的断点续传。秒传继续创建文件引用，断点续传继续返回原 `upload_id` 与分片进度；只有需要创建新任务的请求返回 `503 + 50012 UploadTaskCreationDisabled`。拒绝发生在配额预留和任务 INSERT 之前，不会回退为 local staging，也不会改写既有任务的 backend/prefix。已有任务的分片、完成和取消接口不受该开关影响，但必须路由到理解其 schema 和 staging 描述符的兼容版本。
>
> **回滚冻结语义**：运维人员将上传入口切换到 freeze 模式后，受信网关必须在鉴权和业务路由之前拦截全部 `/api/file/upload/**` 请求，稳定返回 `503 + 50013 UploadLifecycleFrozen`、`Retry-After` 和 `Cache-Control: no-store`。该返回表示 init/chunk/complete/cancel 均被冻结，不等价于只停止创建新任务的 50012。冻结不改写任务状态或租约；恢复必须先让兼容版本重新承接上传路由。

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误、缺少必填参数 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 字段值不符合规则、Range 格式无效 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 50005 | `FileNotFound` | 文件不存在 | 指定的 file_id 不存在或不属于当前用户 |
| 416 | 10002 | `ValidationFailed` | 请求范围无效 | Range 范围超出文件大小 |
| 503 | 50012 | `UploadTaskCreationDisabled` | New upload task creation is temporarily disabled | 回滚截止已关闭，且本次初始化需要创建新的非秒传任务 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "file_id": 123,
    "filename": "document.pdf",
    "file_size": 104857600,
    "file_hash": "d41d8cd98f00b204e9800998ecf8427e",
    "mime_type": "application/pdf",
    "supports_range": true
  }
}
```

---

### 4.2 上传分片

**POST** `/api/file/upload/chunk`

#### 实现状态
**已实现：接口通过后端无关的 `UploadStagingStorage` 写入不可变分片；生产安全模式要求共享 S3/MinIO 暂存，对象写入成功但数据库未记录等孤儿工件由持久 `storage_jobs` cleanup/reconciliation 收敛。local 暂存仅保留给开发测试和迁移期存量任务排空。**

上传文件分片数据。

#### 请求头

```
Authorization: Bearer <access_token>
Content-Type: application/octet-stream
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |
| Content-Type | 是 | application/octet-stream |

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| upload_id | string | 是 | 上传会话 ID（从初始化上传获取） |
| chunk_index | integer | 是 | 分片索引（从 0 开始） |
| chunk_hash | string | 是 | 分片 MD5 哈希（32 字符小写十六进制） |

#### 请求体

二进制数据（原始分片内容）

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误、缺少必填参数 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | chunk_hash 格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 400 | 50008 | `UploadTaskNotFound` | 上传任务不存在 | upload_id 不存在或已过期 |
| 400 | 50009 | `ChunkVerifyFailed` | 分片校验失败 | 分片哈希与实际数据不匹配 |
| 409 | 10004 | `ResourceConflict` | 资源冲突 | 任务已进入完成/取消/过期等不再接受分片的状态 |

#### 幂等与跨实例语义

- 分片对象保存在共享 S3/MinIO staging 前缀，请求不要求命中初始化上传的 API 实例。
- 同一 `upload_id + chunk_index + chunk_hash` 可安全重复提交，成功响应保持 `uploaded=true`。
- 服务端在对象写入前校验任务和分片几何信息，写入后仅在任务仍为 `InProgress` 时记录分片。
- 如果完成、取消或过期在分片写入期间获胜，迟到请求返回 `409 + 10004`，不得改变终态；已产生的孤儿对象由清理任务回收。
- `uploaded_chunks` 和完成覆盖判断只以 PostgreSQL `upload_task_chunks` 为准，不以对象列表或进程内缓存为准。
- `expires_at` 在创建任务时由 PostgreSQL `NOW() + TTL` 生成；分片、取消、断点续传和过期转换均以数据库 `NOW()` 判断，不得用 API 进程本地时区解析 `TIMESTAMP` 后授予或拒绝写权限。

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "chunk_index": 0,
    "uploaded": true
  }
}
```

---

### 4.3 完成上传

**POST** `/api/file/upload/complete`

#### 实现状态
**同步接口、PostgreSQL 完成租约、S3-native 暂存、持久清理 Worker、过期接管与 `completed_file_id` 幂等重放均已实现**

完成文件上传，合并所有分片。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 请求参数

```json
{
  "upload_id": "abc123def456"
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| upload_id | string | 是 | 上传会话 ID |

> **✅ 上传完成配额转换（CRITICAL）**：本接口执行**预占用转换为实际使用**：
> - `storage_reserved` 减少 `file_size`（释放预占用）
> - `storage_used` 增加 `file_size`（记录实际使用）
> - 原子性操作，确保配额一致性

#### 分布式完成与重试语义

完成接口保持同步响应，但由 PostgreSQL 状态机和有期限租约保证跨实例单飞：

| 当前状态 | 行为 |
|----------|------|
| `InProgress` | 完整覆盖校验通过后，当前请求原子认领 `Finalizing` 租约并执行完成流程 |
| `Finalizing`，租约有效 | 返回 `409 + 10004 ResourceConflict`；响应可携带 `Retry-After`，客户端稍后使用同一 `upload_id` 重试 |
| `Finalizing`，租约过期 | 当前请求可通过条件更新接管并恢复完成流程 |
| `Completed` | 幂等返回首次完成创建的同一 `file`，不重新组装、不重复结算配额 |
| `Cancelled` / `Expired` / `Failed` | 返回 `400 + 50008 UploadTaskNotFound`，不得恢复为进行中 |

完成过程按“租约认领 → 事务外对象组装/校验 → 短事务提交 → 异步清理”执行。服务端在组装后、最终 Blob 晋升后分别用最新 `state_version` 续租；最终事务的首条业务语句还必须在该事务连接上再次续租并锁定当前 generation，事务末尾继续执行完成 CAS。任一次续租或最终 CAS 未命中时，当前请求失去完成权限并返回 `409 + 10004 ResourceConflict`，不得提交文件、配额或上传终态。这样即使数据库网络分区使旧 owner 的事务开始被延迟到租约过期和新 owner 完成之后，恢复的旧请求也只能回滚。服务端验证分片对象、总大小、整文件 MD5 与 SHA-256；multipart ETag 不作为文件 MD5。数据库分片描述符对应的对象缺失、大小、ETag 或内容哈希不一致时返回 `400 + 50009 ChunkVerifyFailed`，并持久化 `upload_staging_mismatch` finding 与一项去重的 staging 对账任务。最终事务提交但 HTTP 响应丢失后，客户端必须以同一身份和同一 `upload_id` 重复调用；服务端从 `completed_file_id` 恢复原成功响应，不等待租约、不重新提升 final Blob、不增加完成尝试或重复结算。重放还必须推进共享文件列表缓存代际，使事务提交后、首次缓存失效前退出的实例不会留下持续到 TTL 的旧列表。超时或断连只表示结果未知，禁止另建上传、直接改写任务，或人工删除 final/staging 对象来“回滚”；staging 仅由已入队的幂等 cleanup 任务清理。

PostgreSQL 租约是同一上传完成权的唯一判断来源。存储层的 `AssemblyConcurrencyLimiter` 不接收、不缓存 `upload_id`，只限制当前实例的组装在途数量；本机槽位耗尽时返回 `429 + 10005 TooManyRequests`，不会把本机状态解释为上传所有权，也不会替代上述 `409` 租约冲突语义。

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | upload_id 为空或格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 400 | 50008 | `UploadTaskNotFound` | 上传任务不存在 | upload_id 不存在或已过期 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 分片不完整、哈希校验失败 |
| 400 | 50009 | `ChunkVerifyFailed` | 分片校验失败 | DB 分片描述符对应对象缺失或元数据/内容不一致；服务端已排入对账 |
| 409 | 10004 | `ResourceConflict` | 资源冲突 | 其他实例持有有效完成租约，请求应稍后重试 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "file": {
      "id": 123,
      "name": "document.pdf",
      "size": 104857600,
      "hash": "d41d8cd98f00b204e9800998ecf8427e",
      "mime_type": "application/pdf",
      "parent_id": 0,
      "created_at": "2026-02-18T12:30:00Z"
    }
  }
}
```

---

### 4.4 取消上传

**DELETE** `/api/file/upload/{upload_id}`

#### 实现状态
**已实现**

取消上传任务，清理临时数据。

> **❌ 上传取消释放预占用（CRITICAL）**：本接口执行**释放预占用空间**：
> - `storage_reserved` 减少 `file_size`（释放预占用）
> - 在同一事务中迁移任务状态并写入 staging 清理任务
> - 对象实际删除由 Worker 幂等执行，不影响取消结果

#### 幂等与竞态语义

- `InProgress`：只有成功执行 `InProgress -> Cancelled` 条件更新的事务释放 reserved quota。
- `Cancelled`：重复取消返回成功，不再次释放配额。
- `Finalizing` 或 `Completed`：返回 `409 + 10004 ResourceConflict`，取消不能覆盖完成流程。
- `Expired` 或 `Failed`：返回 `400 + 50008 UploadTaskNotFound`。
- 取消事务提交后，即使对象清理暂时失败，任务仍保持 `Cancelled`；Worker 负责重试清理。

#### 状态版本与日志关联

- `InProgress -> Cancelled` 的同一条件更新必须把 `state_version` 递增一次并返回更新后的版本；配额释放、staging cleanup 入队和分片元数据删除与该更新在同一事务提交。
- 重复取消读取并返回内部持久版本供日志使用，不再次递增 `state_version`；公开成功响应仍保持 `data: null`。
- Controller 从请求属性创建固定 `operation=upload_cancel` 的类型化日志上下文，并把非空路径 `upload_id` 按值传过 Service、Lifecycle 和数据库边界。成功迁移与重放日志使用数据库返回的 `state_version`；取消路径不持有完成租约，`lease_owner` 为 `null`；请求侧入队接口不返回持久任务主键时 `job_id` 为 `null`。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| upload_id | string | 上传会话 ID |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | upload_id 为空 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 400 | 50008 | `UploadTaskNotFound` | 上传任务不存在 | upload_id 不存在或不属于当前用户 |
| 409 | 10004 | `ResourceConflict` | 资源冲突 | 上传正在完成或已经完成，不能取消 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": null
}
```

---

### 4.5 获取下载信息

**GET** `/api/file/download/{file_id}/info`

#### 实现状态
**已实现**

获取文件下载元数据（不包含文件内容），用于下载前预检。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| file_id | integer | 文件 ID |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | file_id 格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 50005 | `FileNotFound` | 文件不存在 | file_id 不存在或不属于当前用户 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "file_id": 123,
    "filename": "document.pdf",
    "file_size": 104857600,
    "file_hash": "d41d8cd98f00b204e9800998ecf8427e",
    "mime_type": "application/pdf",
    "supports_range": true
  }
}
```

---

### 4.6 下载文件

**GET** `/api/file/download/{file_id}`

#### 实现状态
**已实现**

下载文件内容，支持 Range 请求。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| file_id | integer | 文件 ID |

#### 下载请求头（可选）

| Header | 说明 |
|--------|------|
| Range | 可选，字节范围，如 `bytes=0-1048575` |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误、缺少必填参数 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 字段值不符合规则 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 50005 | `FileNotFound` | 文件不存在 | 指定的 file_id 不存在或不属于当前用户 |
| 500 | 50011 | `FileReadError` | 文件读取失败 | 文件元数据存在，但最终对象缺失、长度不一致或无法打开读取流 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

#### 响应头

| Header | 说明 |
|--------|------|
| Content-Type | 文件 MIME 类型 |
| Content-Length | 返回数据长度 |
| Content-Disposition | `attachment; filename="document.pdf"` |
| Accept-Ranges | `bytes` |
| Content-Range | 范围信息（Range 请求时） |

#### Range 请求语义

本接口支持 HTTP Range 请求，遵循 [RFC 7233](https://datatracker.ietf.org/doc/html/rfc7233) 规范，用于断点续传和分片下载场景。

| 场景 | 请求头 | HTTP 状态码 | 说明 |
|------|--------|------------|------|
| **完整下载** | 无 Range | `200 OK` | 返回完整文件内容 |
| **范围下载** | `Range: bytes=0-1023` | `206 Partial Content` | 返回指定字节范围的内容 |
| **范围无效** | `Range: bytes=999999-`（超出文件大小） | `416 Range Not Satisfiable` | 范围无法满足，响应体包含错误信息 |

**Range 请求格式说明**：
- 仅支持 `bytes` 单位
- 格式：`bytes=<start>-<end>`（end 可选，表示到文件末尾）
- 示例：`Range: bytes=0-1048575`（下载前 1MB）

#### 响应状态码矩阵

| HTTP 状态码 | 触发条件 | 响应体 |
|------------|----------|--------|
| `200 OK` | 无 Range 请求，返回完整文件 | 文件二进制数据流 |
| `206 Partial Content` | 有效 Range 请求 | 请求范围的字节片段 |
| `416 Range Not Satisfiable` | Range 范围无效或超出文件大小 | JSON 错误响应（见下方示例） |
| `500 Internal Server Error` | 响应开始前发现最终对象缺失、长度不符或无法打开 | JSON 错误响应，业务码 `50011` |

下载响应开始后若对象存储 Range 流提前结束，HTTP 状态和响应体已不能切换为 JSON 错误；服务端
关闭流，客户端按 `Content-Length` 观察到不完整传输。该事件会以内容 ID 记录为
`final_blob_read_interrupted` 对账 finding，客户端应使用 Range 重试，不得把短响应视为成功文件。

#### 416 Range Not Satisfiable 错误响应

当请求的 Range 范围无法满足时（如起始位置超出文件大小），返回 `416` 状态码：

```json
{
  "code": 10002,
  "message": "请求范围无效",
  "data": {
    "file_size": 104857600,
    "requested_range": "bytes=999999000-",
    "reason": "请求的起始位置超出文件大小"
  }
}
```

#### 响应

文件二进制数据流。

---

### 4.7 获取文件列表

**GET** `/api/file/list`

#### 实现状态
**已实现**

获取指定目录下的文件和文件夹列表。

> **混合返回说明**：`items` 数组同时包含文件和文件夹对象，通过 `type` 字段区分（`"file"` 或 `"folder"`）。两种类型的字段结构不同：
> - **文件夹**：包含 `item_count`（子项数量），无 `size`、`mime_type`、`hash`
> - **文件**：包含 `size`、`mime_type`、`hash`，无 `item_count`

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| parent_id | integer | 否 | 父文件夹 ID，默认 0 |
| page | integer | 否 | 页码，默认 1 |
| page_size | integer | 否 | 每页数量，默认 20，最大 100 |
| sort_by | string | 否 | 排序字段：name/size/created_at/updated_at |
| sort_order | string | 否 | 排序方向：asc/desc，默认 asc |
| type | string | 否 | 筛选类型：all/file/folder，默认 all |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误、缺少必填参数 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 字段值不符合规则 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 50006 | `FolderNotFound` | 文件夹不存在 | 指定的 parent_id 不存在或不属于当前用户 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "items": [
      {
        "id": 1,
        "name": "工作文档",
        "type": "folder",
        "item_count": 15,
        "created_at": "2026-01-10T08:00:00Z",
        "updated_at": "2026-01-12T14:30:00Z"
      },
      {
        "id": 2,
        "name": "报告.docx",
        "type": "file",
        "size": 102400,
        "mime_type": "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        "hash": "a1b2c3d4e5f6...",
        "created_at": "2026-01-11T10:00:00Z",
        "updated_at": "2026-01-11T10:00:00Z"
      }
    ],
    "pagination": {
      "page": 1,
      "page_size": 20,
      "total": 50,
      "total_pages": 3
    }
  }
}
```

---

### 4.8 搜索文件

**GET** `/api/file/search`

#### 实现状态
**已实现**

根据关键词搜索文件和文件夹。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| keyword | string | 是 | 搜索关键词，1-100字符 |
| type | string | 否 | 筛选类型：all/file/folder，默认 all |
| folder_id | integer | 否 | 限定搜索范围，不指定则全局搜索 |
| page | integer | 否 | 页码，默认 1 |
| page_size | integer | 否 | 每页数量，默认 20，最大 100 |

#### 业务规则

1. 支持文件名/文件夹名模糊搜索（LIKE %keyword%）
2. 搜索范围限定：
   - 不指定 `folder_id`：搜索用户所有文件
   - 指定 `folder_id`：仅搜索指定文件夹下的内容
3. 关键词过滤：禁止 `%`、`_`、`\`、`'`、`"` 等特殊字符（防止 SQL 注入）
4. 结果按名称排序（升序）
5. 返回结果包含路径面包屑信息

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误、缺少必填参数 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | keyword 为空、长度超限、包含禁止字符 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |

**40002 ValidationFailed 响应示例**：

```json
{
  "code": 10002,
  "message": "参数校验失败: 参数 'keyword' 长度必须在 1-100 字符之间",
  "data": null
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "items": [
      {
        "id": 5,
        "name": "项目报告.docx",
        "type": "file",
        "size": 102400,
        "mime_type": "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        "hash": "a1b2c3d4e5f6...",
        "path": "/工作文档/项目报告.docx",
        "created_at": "2026-01-11T10:00:00Z",
        "updated_at": "2026-01-11T10:00:00Z"
      },
      {
        "id": 12,
        "name": "项目资料",
        "type": "folder",
        "item_count": 8,
        "path": "/项目资料",
        "created_at": "2026-01-10T08:00:00Z",
        "updated_at": "2026-01-12T14:30:00Z"
      }
    ],
    "pagination": {
      "page": 1,
      "page_size": 20,
      "total": 15,
      "total_pages": 1
    }
  }
}
```

---

### 4.9 获取文件详情

**GET** `/api/file/{file_id}`

#### 实现状态
**✅ 已实现**

获取单个文件的详细信息。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| file_id | integer | 文件 ID |

#### 响应字段

| 字段 | 类型 | 说明 |
|------|------|------|
| id | integer | 文件 ID |
| name | string | 文件名 |
| type | string | 类型（固定为 "file"） |
| size | integer | 文件大小（字节） |
| hash | string | 文件 MD5 哈希 |
| mime_type | string | MIME 类型 |
| parent_id | integer | 父文件夹 ID（0 表示根目录） |
| path | string | 文件路径 |
| created_at | string | 创建时间 |
| updated_at | string | 更新时间 |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误、缺少必填参数 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 50005 | `FileNotFound` | 文件不存在 | 指定的 file_id 不存在或不属于当前用户 |

#### 成功响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "id": 123,
    "name": "document.pdf",
    "type": "file",
    "size": 104857600,
    "hash": "d41d8cd98f00b204e9800998ecf8427e",
    "mime_type": "application/pdf",
    "parent_id": 0,
    "path": "/document.pdf",
    "created_at": "2026-01-13T10:00:00Z",
    "updated_at": "2026-01-13T10:00:00Z"
  }
}
```

---

### 4.10 重命名文件

**PUT** `/api/file/{file_id}/rename`

#### 实现状态
**已实现**

重命名文件或文件夹。

> **文件/文件夹歧义说明**：`file_id` 参数可以是文件 ID 或文件夹 ID，服务端通过查询 `files` 表和 `folders` 表自动判断类型。无论哪种类型，操作语义相同——修改名称字段。
>
> **命名冲突规则**：如果目标名称在同目录下已存在（无论是文件还是文件夹），返回 `409 + 50007 FileAlreadyExists`。注意：文件和文件夹可以同名共存（如 `doc.pdf` 文件和 `doc.pdf` 文件夹），重命名时仅检查同类型冲突。
>
> **并发语义**：同一父目录下的多个文件并发重命名为同一合法名称时，恰好一个请求成功，其余请求稳定返回 `409 + 50007 FileAlreadyExists`。输家不得被误报为 `FileNotFound`，也不得暴露 PostgreSQL 唯一约束异常。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| file_id | integer | 文件/文件夹 ID |

#### 请求参数

```json
{
  "new_name": "新名称.pdf"
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| new_name | string | 是 | 新名称，1-255 字符，必须是合法 UTF-8；禁止 `/ \ : * ? " < > |`、控制字符、`.`、`..` 和以 `.` 开头 |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误、缺少必填参数 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 字段值不符合规则 |
| 400 | 50001 | `InvalidFilename` | 文件名无效 | 新文件名包含非法字符或不符合命名规则 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 50005 | `FileNotFound` | 文件不存在 | 指定的 file_id 不存在或不属于当前用户 |
| 409 | 50007 | `FileAlreadyExists` | 文件已存在 | 目标目录下已存在同名文件 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "id": 123,
    "name": "新名称.pdf",
    "updated_at": "2026-01-13T11:00:00Z"
  }
}
```

---

### 4.11 移动文件

**PUT** `/api/file/move`

#### 实现状态
**已实现**

移动文件或文件夹到指定目录。

> **批量操作行为**：`file_ids` 支持批量移动，每个项目独立处理。如果部分项目移动失败（如权限不足、源不存在），其他成功的项目仍会完成移动，响应中返回 `moved_count` 表示成功数量。
>
> **目标文件夹验证**：`target_folder_id` 必须属于当前用户，否则返回 `404 + 50006 FolderNotFound`。`target_folder_id = 0` 表示移动到根目录。
>
> **并发同名语义**：多个请求把不同目录下的同名文件或同名文件夹并发移动到同一目标目录时，每种同类型名称恰好一个项目完成移动；其余同名项按既有批量冲突规则跳过，请求仍返回 HTTP 200/业务码 0，且该请求对应的 `moved_file_count`、`moved_folder_count` 与 `moved_count` 不计入冲突项。输家保留原目录、路径与完整子树位置，源/目标目录计数保持精确，服务端不得暴露 PostgreSQL 唯一约束异常。
>
> **同一文件夹并发移动语义**：多个请求把同一文件夹并发移动到不同合法目标目录时，请求按数据库事务顺序执行；每个请求仍可返回 HTTP 200/业务码 0 并计入一次文件夹移动，但后执行者必须基于前一事务提交后的最新父目录更新根与完整子树。最终源、经过的中间目标与最终目标目录 `item_count` 必须全部匹配实际直属项，不得重复扣减过期父目录或遗留中间目标计数。
>
> **文件夹移动原子性**：根位置、任一后代文件夹路径、任一后代文件路径或源/目标目录 `item_count` 更新未命中预期行时，整笔移动必须回滚并返回 `500 + 10006 Failed to move items`；不得提交部分子树路径、部分父目录计数或成功响应。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 请求参数

```json
{
  "file_ids": [1, 2, 3],
  "target_folder_id": 10
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| file_ids | array | 是 | 文件/文件夹 ID 列表 |
| target_folder_id | integer | 是 | 目标文件夹 ID |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误、缺少必填参数 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 字段值不符合规则 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 50005 | `FileNotFound` | 文件不存在 | 待移动项中包含不存在或无权限访问的文件 |
| 404 | 50006 | `FolderNotFound` | 文件夹不存在 | 目标文件夹不存在或不属于当前用户 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "moved_count": 3
  }
}
```

---

### 4.12 复制文件

**POST** `/api/file/copy`

#### 实现状态
**已实现**

复制文件或文件夹到指定目录。

> **存储配额检查**：复制操作会增加用户的 `storage_used`，服务端必须在操作前检查配额。如果复制后超过配额，返回 `400 + 50004 StorageQuotaExceeded`，不执行任何复制。
>
> **内容引用计数**：文件复制采用元数据复制，实际文件内容（`file_contents` 表）通过引用计数共享。复制文件时：
> - 在 `files` 表创建新记录
> - `file_contents.ref_count` 递增（而非复制物理存储）
> - 删除文件时 `ref_count` 递减，仅当 `ref_count = 0` 时删除物理文件
>
> **并发同名语义**：多个请求把同一文件或不同目录下的同名文件并发复制到同一目标目录时，恰好一个副本创建成功；其余同名项按批量冲突规则跳过，请求仍返回 HTTP 200/业务码 0，且该请求的 `copied_file_count` 与 `copied_count` 不计入冲突项。输家不得增加内容引用计数或已用配额，全部预留配额必须释放，服务端不得暴露 PostgreSQL 唯一约束异常。
>
> **文件复制目标原子性**：显式文件复制到非根目录时，批次事务必须在名称锁后锁定目标目录，并以锁定行的最新路径插入文件；目标目录 `item_count` 必须在同一事务中按实际成功文件数增加。目标不存在或计数更新未命中时，批次的文件元数据、内容引用和已用配额必须整体回滚，预留配额必须释放，复制计数不得包含该批次。目标目录并发移动必须与复制串行，最终文件路径和直属计数属于同一目录层级。
>
> **并发文件夹复制语义**：多个请求把同一文件夹或不同父目录下的同名文件夹并发复制到同一目标目录时，恰好一个完整子树创建成功；其余同名根按批量冲突规则跳过，请求仍返回 HTTP 200/业务码 0，且该请求的 `copied_folder_count`、`copied_file_count` 与 `copied_count` 均不计入被跳过子树。输家不得插入任何子树节点、增加内容引用或已用配额、更新目标目录计数，全部预留配额必须释放，服务端不得暴露 PostgreSQL 唯一约束异常。
>
> **文件夹复制计数原子性**：完整子树插入后，目标目录 `item_count` 更新未命中预期行时，该子树事务必须整体回滚；请求沿用批量部分成功信封返回 HTTP 200/业务码 0，三个复制计数均不计入该子树。不得遗留根/后代元数据、内容引用、已用配额或预留配额，目标计数保持原值。
>
> **目标目录并发移动语义**：复制文件夹子树期间目标目录被并发移动时，复制事务必须与目标根移动串行，并以取得目标行锁后的最新 `path/depth` 构造完整副本路径；随后执行的目标移动也必须覆盖已经提交的副本子树。最终目标、复制根、后代目录和文件路径必须属于同一目录层级，目标 `item_count` 与实际直属项一致。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 请求参数

```json
{
  "file_ids": [1, 2, 3],
  "target_folder_id": 10
}
```

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误、缺少必填参数 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 字段值不符合规则 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 50005 | `FileNotFound` | 文件不存在 | 待复制项中包含不存在或无权限访问的文件 |
| 404 | 50006 | `FolderNotFound` | 文件夹不存在 | 目标文件夹不存在或不属于当前用户 |
| 409 | 50004 | `StorageQuotaExceeded` | 存储空间不足 | 复制后将超过用户存储配额 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "copied_count": 3,
    "new_files": [
      {"old_id": 1, "new_id": 101},
      {"old_id": 2, "new_id": 102},
      {"old_id": 3, "new_id": 103}
    ]
  }
}
```

---

### 4.13 删除文件

**DELETE** `/api/file`

#### 实现状态
**已实现**

删除文件或文件夹（移入回收站）。

> **🗑️ 软删除语义（CRITICAL）**：本接口执行的是**移入回收站**操作，而非物理删除：
> - 文件/文件夹从原位置移除，添加到 `trash` 表
> - **存储配额不释放**：`users.storage_used` 不会减少，文件仍占用空间
> - **预占用不受影响**：删除操作不影响 `storage_reserved`，仅针对已完成上传的文件
> - **可恢复**：用户可通过回收站 API（第 6 节）恢复误删文件
> - **自动清理**：回收站项目 30 天后自动彻底删除，届时才释放存储空间
>
> **显式文件父计数原子性**：把非根目录中的显式文件移入回收站时，必须在同一 PostgreSQL 事务内按实际移除数扣减来源目录 `item_count`；计数更新未命中时，回收站插入、分享清理、活跃文件删除和计数变化全部回滚。被文件夹删除覆盖的文件不单独扣减其内部父目录。
>
> **文件夹根父计数原子性**：把文件夹子树移入回收站时，只对每个锁后顶层删除根的外部非根父目录 `item_count - 1`；同一请求中被其他删除根覆盖的后代不重复扣减，子树内部目录也不逐层更新。父计数更新未命中时，快照插入、分享清理、活跃子树删除和全部父计数变化必须整体回滚。
>
> **并发子树封闭语义**：文件夹移入回收站时，服务端必须在同一删除事务内锁定当前完整子树并在锁后生成快照。与复制、上传、创建或移动并发时，先提交的后代写入必须包含在 `trash.item_data` 中并随子树移除；等待子树目录锁的写入必须因目标计数更新未命中而回滚。成功响应后不得遗留引用已删除 `folder_id`/`parent_id` 的活跃文件或文件夹，快照中的文件集合必须与本次移除集合一致。
>
> **相关接口**：
> - 查看回收站：`GET /api/trash`（6.1）
> - 恢复文件：`POST /api/trash/restore`（6.2）
> - 彻底删除：`DELETE /api/trash`（6.3）— 释放存储空间

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 请求参数

```json
{
  "file_ids": [1, 2, 3]
}
```

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误、缺少必填参数 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 字段值不符合规则 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 50005 | `FileNotFound` | 文件不存在 | 待删除项中包含不存在或无权限访问的文件 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "deleted_count": 3
  }
}
```

---

## 5. 文件夹接口

### 5.1 创建文件夹

**POST** `/api/folder/create`

#### 实现状态
**已实现**

在指定目录下创建新文件夹。支持层级目录结构，无嵌套深度限制。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 请求参数

```json
{
  "name": "新建文件夹",
  "parent_id": 0
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| name | string | 是 | 文件夹名称，1-255 字符，必须是合法 UTF-8；禁止 `/ \ : * ? " < > |`、控制字符、`.`、`..` 和以 `.` 开头 |
| parent_id | integer | 否 | 父文件夹 ID，默认 0（根目录） |

#### 文件夹名称约束

| 约束项 | 规则 | 违反后果 |
|--------|------|----------|
| **长度** | 1-255 字符 | 返回 `400 + 10002` 校验失败 |
| **禁止字符** | 禁止以下字符：<br>- `/ \ : * ? " < > |`（跨平台文件系统保留字符）<br>- Unicode 控制字符（含 0x00-0x1F、0x7F-0x9F） | 返回 `400 + 50001` 文件名无效 |
| **保留名称** | 禁止 `.` 和 `..`（相对路径标识） | 返回 `400 + 50001` 文件名无效 |
| **隐藏文件夹** | 禁止以 `.` 开头（Unix 隐藏文件约定） | 返回 `400 + 50001` 文件名无效 |
| **字符集** | 允许合法 UTF-8 字符（含中文、emoji 等），拒绝非法 UTF-8 字节序列 | 返回 `400 + 50001` 文件名无效 |
| **首尾空格** | 自动去除首尾空格后验证 | - |

**有效文件夹名称示例**：
- ✅ `Documents`
- ✅ `工作文件2024`
- ✅ `毕业论文_最终版.doc`
- ✅ `📁资料`
- ✅ `Project_Alpha`
- ✅ `用户数据.backup`

**无效文件夹名称示例**：
- ❌ `My/Folder`（包含 `/`）
- ❌ `Folder:Name`（包含 `:`）
- ❌ `.`（保留名称）
- ❌ `..`（保留名称）
- ❌ `.hidden`（以 `.` 开头）
- ❌ `Folder\u0001Name`（包含控制字符）
- ❌ 非法 UTF-8 字节序列

**同名规则**：
- 文件和文件夹可以同名（允许 `Document.pdf` 和 `Document/` 共存）
- 同一目录下不允许存在同名文件夹（返回 `409 + 50010`）
- 嵌套文件夹创建时，父目录 `item_count` 增量与新文件夹插入在同一 PostgreSQL 事务提交；任一步失败均不得留下文件夹或计数偏差。
- 并发创建同一用户、父目录和名称时，数据库唯一约束只允许一个成功请求，其余请求稳定返回 `409 + 50010`，不得返回内部错误或重复增加父目录计数。

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
| |------------|--------|----------|----------|----------|
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 文件夹名称长度无效（空或超过255字符） |
| 400 | 50001 | `InvalidFilename` | 文件名无效 | 包含禁止字符、保留名称、隐藏文件夹、非法 UTF-8 或控制字符 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 `Authorization` |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | `Authorization` 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 50006 | `FolderNotFound` | 文件夹不存在 | 指定的 `parent_id` 不存在或不属于当前用户 |
| 409 | 50010 | `FolderAlreadyExists` | 同名文件夹已存在 | 同一目录下已存在同名文件夹 |

**10002 ValidationFailed 响应示例**：

```json
{
  "code": 10002,
  "message": "参数校验失败",
  "data": {
    "field": "name",
    "reason": "文件夹名称长度必须在 1-255 字符之间",
    "invalid_value": ""
  }
}
```

**50001 InvalidFilename 响应示例**：

```json
{
  "code": 50001,
  "message": "文件名无效",
  "data": {
    "field": "name",
    "reason": "文件夹名称包含禁止字符：/ \\ : * ? \" < > | 或控制字符",
    "invalid_value": "My/Folder"
  }
}
```

**50006 FolderNotFound 响应示例**：

```json
{
  "code": 50006,
  "message": "文件夹不存在",
  "data": {
    "folder_id": 99999,
    "reason": "指定的父文件夹不存在或不属于当前用户"
  }
}
```

**50010 FolderAlreadyExists 响应示例**：

```json
{
  "code": 50010,
  "message": "同名文件夹已存在",
  "data": {
    "name": "新建文件夹",
    "parent_id": 0,
    "existing_folder_id": 10,
    "reason": "同一目录下已存在同名文件夹"
  }
}
```

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

**40107 TokenMalformed 响应示例**：

```json
{
  "code": 40107,
  "message": "令牌格式错误",
  "data": {
    "reason": "Authorization 头格式应为 'Bearer <token>'"
  }
}
```

**40108 TokenExpired 响应示例**：

```json
{
  "code": 40108,
  "message": "令牌已过期",
  "data": {
    "token_type": "access_token",
    "expired_at": "2026-01-13T12:00:00Z"
  }
}
```

#### 成功响应字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| id | integer | 新建文件夹的唯一标识符 |
| name | string | 文件夹名称（已去除首尾空格） |
| parent_id | integer | 父文件夹 ID，0 表示根目录 |
| path | string | 文件夹完整路径，从根目录开始 |
| created_at | string | 创建时间，ISO 8601 格式 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "id": 10,
    "name": "新建文件夹",
    "parent_id": 0,
    "path": "/新建文件夹",
    "created_at": "2026-01-13T11:00:00Z"
  }
}
```

---

### 5.2 获取目录树

**GET** `/api/folder/tree`

#### 实现状态
**已实现**

获取文件夹目录树结构。

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| parent_id | integer | 否 | 起始文件夹 ID，默认 0 |
| depth | integer | 否 | 展开深度，默认 -1（全部） |

#### 请求头

```
Authorization: Bearer <access_token>
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "id": 0,
    "name": "根目录",
    "children": [
      {
        "id": 1,
        "name": "文档",
        "children": [
          {"id": 3, "name": "工作", "children": []},
          {"id": 4, "name": "学习", "children": []}
        ]
      },
      {
        "id": 2,
        "name": "图片",
        "children": []
      }
    ]
  }
}
```

---

### 5.3 获取路径面包屑

**GET** `/api/folder/{folder_id}/breadcrumb`

获取当前文件夹的路径面包屑。

#### 实现状态
**已实现**

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| folder_id | integer | 文件夹 ID |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | folder_id 格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 50006 | `FolderNotFound` | 文件夹不存在 | 指定的 folder_id 不存在或不属于当前用户 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

**50006 FolderNotFound 响应示例**：

```json
{
  "code": 50006,
  "message": "文件夹不存在",
  "data": {
    "folder_id": 99999,
    "reason": "指定的文件夹不存在或不属于当前用户"
  }
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "path": [
      {"id": 0, "name": "根目录"},
      {"id": 1, "name": "文档"},
      {"id": 3, "name": "工作"}
    ]
  }
}
```

---

### 5.4 重命名文件夹

**PUT** `/api/folder/{folder_id}/rename`

#### 实现状态
**已实现**

重命名当前用户拥有的文件夹，并在同一 PostgreSQL 事务内同步更新子树中文件夹和文件的路径。
服务在锁定目标文件夹后获取 `(user_id, parent_id, new_name)` 名称级事务锁，并在锁内执行同名检查；
并发把同一父目录下多个文件夹重命名为同一名称时，恰好一个请求成功，其余请求稳定返回
`409 + 50010`，不得暴露唯一约束内部错误或提交部分路径更新。

#### 请求参数

```json
{
  "new_name": "新文件夹名"
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| folder_id | integer | 是 | 路径中的文件夹 ID，必须为正整数 |
| new_name | string | 是 | 新名称，遵循 5.1 的文件夹名称约束 |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 触发场景 |
|------------|--------|----------|----------|
| 400 | 10001 | `InvalidParameter` | folder_id 格式错误 |
| 400 | 10002 | `ValidationFailed` | new_name 长度无效 |
| 400 | 50001 | `InvalidFilename` | new_name 违反文件夹名称约束 |
| 404 | 50006 | `FolderNotFound` | 文件夹不存在或不属于当前用户 |
| 409 | 50010 | `FolderAlreadyExists` | 同一父目录已存在同名文件夹 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "id": 10,
    "name": "新文件夹名",
    "path": "/新文件夹名/",
    "updated_at": "2026-01-13 11:30:00"
  }
}
```

---

## 6. 回收站接口

### 6.1 获取回收站列表

**GET** `/api/trash`

#### 实现状态
**已实现**

获取回收站中的文件列表。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| page | integer | 否 | 页码，默认 1 |
| page_size | integer | 否 | 每页数量，默认 20 |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "items": [
      {
        "id": 1,
        "original_id": 123,
        "name": "已删除文件.pdf",
        "type": "file",
        "size": 102400,
        "original_path": "/文档/已删除文件.pdf",
        "deleted_at": "2026-01-10T10:00:00Z",
        "expires_at": "2026-02-09T10:00:00Z"
      }
    ],
    "pagination": {
      "page": 1,
      "page_size": 20,
      "total": 10,
      "total_pages": 1
    }
  }
}
```

#### 空回收站响应示例

当回收站为空时，`items` 返回空数组，`pagination.total` 为 0：

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "items": [],
    "pagination": {
      "page": 1,
      "page_size": 20,
      "total": 0,
      "total_pages": 0
    }
  }
}
```

---

### 6.2 恢复文件

**POST** `/api/trash/restore`

#### 实现状态
**已实现**

从回收站恢复文件。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 请求参数

```json
{
  "trash_ids": [1, 2, 3]
}
```

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 10003 | `ResourceNotFound` | 资源不存在 | trash_id 不存在或不属于用户 |

#### 实现说明

回收站彻底删除（DELETE /api/trash）和清空（DELETE /api/trash/all）由 `TrashService::Delete` 和 `TrashService::DeleteAll` 处理。批量删除时逐项串行执行，每项内部先将文件元数据从 trash 表移除，再通过当前 `BlobStore` 后端并行执行 blob 清理（最大并发 4），最后对 `file_contents.ref_count` 递减。引用计数归零时最终 blob 由存储工作线程删除，不阻塞 Drogon 事件循环；本地和 S3/MinIO 后端保持相同 API 响应结构。

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

#### 业务规则

1. **目标位置恢复**：优先恢复到原始位置；如原始父文件夹已不存在，则恢复到用户根目录
2. **命名冲突处理**：如目标位置存在同名文件/文件夹，自动重命名为 `name (n).ext` 格式（n 从 1 开始递增）
   - 示例：`文档.pdf` → `文档 (1).pdf` → `文档 (2).pdf`
   - 文件夹同理：`工作` → `工作 (1)` → `工作 (2)`
3. **文件恢复原子性**：恢复文件时必须在同一 PostgreSQL 事务内锁定回收站行和目标父目录、确定目标名称、插入活跃文件、对最终非根父目录 `item_count + 1` 并删除回收站行。父计数或回收站删除未命中时全部回滚；并发恢复同一 `trash_id` 只能有一个成功消费者，不得同时留下活跃文件与原回收站项
4. **文件夹恢复语义**：带 `folder_tree` 快照的文件夹项递归恢复完整目录与文件树；历史兼容记录缺少有效快照时只恢复空根文件夹。冲突重命名只作用于恢复根，快照内部名称和层级保持不变
5. **文件夹恢复原子性**：恢复文件夹时必须在同一 PostgreSQL 事务内锁定回收站行、取得候选根名称锁、锁定仍存在的最终非根父目录、重建完整快照、对最终非根父目录 `item_count + 1` 并精确删除回收站行。任一节点插入、父计数或回收站删除未命中时全部回滚；并发恢复同一 `trash_id` 只能有一个成功消费者

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "summary": {
      "total": 3,
      "success_count": 2,
      "failure_count": 1
    },
    "results": [
      {
        "trash_id": 1,
        "status": "success",
        "file_id": 123,
        "path": "/文档/已删除文件.pdf"
      },
      {
        "trash_id": 2,
        "status": "success",
        "file_id": 124,
        "path": "/已删除文件2 (1).pdf"
      },
      {
        "trash_id": 3,
        "status": "failed",
        "error": {
          "code": 10003,
          "message": "资源不存在",
          "field": "trash_id",
          "value": 3
        }
      }
    ]
  }
}
```

---

### 6.3 彻底删除

**DELETE** `/api/trash`

#### 实现状态
**已实现**

彻底删除回收站中的文件。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 请求参数

```json
{
  "trash_ids": [1, 2, 3]
}
```

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 404 | 10003 | `ResourceNotFound` | 资源不存在 | trash_id 不存在或不属于用户 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "summary": {
      "total": 3,
      "success_count": 2,
      "failure_count": 1
    },
    "results": [
      {
        "trash_id": 1,
        "status": "success",
        "freed_space": 102400
      },
      {
        "trash_id": 2,
        "status": "success",
        "freed_space": 102400
      },
      {
        "trash_id": 3,
        "status": "failed",
        "error": {
          "code": 10003,
          "message": "资源不存在",
          "field": "trash_id",
          "value": 3
        }
      }
    ]
  }
}
```

---

### 6.4 清空回收站

**DELETE** `/api/trash/all`

#### 实现状态
**已实现**

清空回收站所有内容。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "deleted_count": 50,
    "freed_space": 10737418240
  }
}
```

---

## 7. 分享接口

### 标识符映射说明

本章中所有 API 使用的 `share_id` 是外部可见的分享标识符（URL 友好格式，如 `sh_abc123`），对应数据库 `shares` 表中的 `share_code` 字段。数据库内部主键 `shares.id` 为自增整数，不直接暴露给 API 调用方。

| 外部标识符 | 数据库字段 | 说明 |
|-----------|-----------|------|
| `share_id` | `shares.share_code` | API 路径/响应中使用的唯一分享标识（如 `sh_abc123`） |
| （内部） | `shares.id` | 数据库自增主键，仅用于内部关联（`share_files.share_id` 外键引用） |

### 7.1 创建分享

**POST** `/api/share`

创建文件分享链接。

#### 实现状态
**已实现**

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 请求参数

```json
{
  "file_ids": [1, 2],
  "expire_days": 7,
  "password": "abc123",
  "permission": "download"
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| file_ids | array | 是 | 文件/文件夹 ID 列表 |
| expire_days | integer | 否 | 有效天数，0 表示永久，默认 7 |
| password | string | 否 | 访问密码，4-8 字符 |
| permission | string | 否 | 权限：view/download，默认 download |

#### 文件夹分享行为说明

`file_ids` 参数支持混合文件和文件夹 ID，两者的分享行为有所不同：

**文件分享**：
- 直接分享单个文件，访客可查看/下载该文件

**文件夹分享**：
- 分享整个文件夹及其**完整内容树**（递归包含所有子文件夹和文件）
- 访客通过 `GET /api/share/browse` 浏览文件夹内容
- 新增到文件夹的内容**不会**自动加入分享（快照语义）
- 文件夹内文件被删除/移动后，分享列表自动更新

> **所有权验证**：所有 `file_ids` 必须属于当前用户（`files.user_id == current_user_id` 或 `folders.user_id == current_user_id`），否则返回 `404 + 50005 FileNotFound`。

> **分享码唯一性**：服务使用密码学随机源生成 8 位大小写字母数字分享码，并以数据库 `uk_shares_code` 约束作为最终唯一性裁决。候选码冲突时，服务在同一创建事务内透明生成新候选并重试，最多尝试 5 个候选；全部冲突时返回现有 `500 + 10006 InternalError`，且不保留 `shares` 或 `share_files` 部分记录。其他数据库错误不得伪装为分享码冲突重试。

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | file_ids 为空或格式错误 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | password 长度不在 4-8 字符之间 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Token 已超过有效期 |
| 404 | 50005 | `FileNotFound` | 文件不存在 | 指定的 file_id 不存在或不属于当前用户 |
| 500 | 10006 | `InternalError` | 服务器内部错误 | 5 个分享码候选均冲突，或创建事务发生其他数据库错误 |

**10002 ValidationFailed 响应示例**：

```json
{
  "code": 10002,
  "message": "参数校验失败",
  "data": {
    "field": "password",
    "reason": "访问密码长度必须在 4-8 字符之间",
    "invalid_value": "ab"
  }
}
```

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

**40108 TokenExpired 响应示例**：

```json
{
  "code": 40108,
  "message": "令牌已过期",
  "data": {
    "token_type": "access_token",
    "expired_at": "2026-01-13T12:00:00Z"
  }
}
```

**50005 FileNotFound 响应示例**：

```json
{
  "code": 50005,
  "message": "文件不存在",
  "data": {
    "file_id": 99999,
    "reason": "指定的文件不存在或不属于当前用户"
  }
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "share_id": "sh_abc123",
    "share_link": "https://disk.example.com/s/abc123",
    "password": "abc123",
    "permission": "download",
    "expires_at": "2026-01-20T10:00:00Z",
    "created_at": "2026-01-13T10:00:00Z"
  }
}
```

---

### 7.2 获取分享列表

**GET** `/api/share`

获取当前用户创建的所有分享。

#### 实现状态
**已实现**

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| status | string | 否 | 状态筛选：all/active/expired/cancelled，默认 all |
| page | integer | 否 | 页码 |
| page_size | integer | 否 | 每页数量 |

**状态筛选与数据库映射**：

| API 参数值 | 数据库 `shares.status` 值 | 说明 |
|-----------|--------------------------|------|
| `all` | （不筛选） | 返回所有状态的分享 |
| `active` | `1` 且未过期 | 有效分享（未过期且未取消） |
| `expired` | `2` | 已过期分享 |
| `cancelled` | `0` | 已取消分享 |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | page 或 page_size 格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Token 已超过有效期 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

**40108 TokenExpired 响应示例**：

```json
{
  "code": 40108,
  "message": "令牌已过期",
  "data": {
    "token_type": "access_token",
    "expired_at": "2026-01-13T12:00:00Z"
  }
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "items": [
      {
        "share_id": "sh_abc123",
        "file_name": "文档.pdf",
        "file_count": 1,
        "share_link": "https://disk.example.com/s/abc123",
        "has_password": true,
        "permission": "download",
        "view_count": 10,
        "download_count": 5,
        "created_at": "2026-01-13T10:00:00Z",
        "expires_at": "2026-01-20T10:00:00Z",
        "status": "active"
      }
    ],
    "pagination": {
      "page": 1,
      "page_size": 20,
      "total": 5,
      "total_pages": 1
    }
  }
}
```

---

### 7.3 获取分享详情

**GET** `/api/share/{share_id}`

获取分享的详细信息。

#### 实现状态
**已实现**

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| share_id | string | 分享 ID |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | share_id 格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Token 已超过有效期 |
| 404 | 60001 | `ShareNotFound` | 分享不存在 | share_id 不存在或不属于当前用户 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

**40108 TokenExpired 响应示例**：

```json
{
  "code": 40108,
  "message": "令牌已过期",
  "data": {
    "token_type": "access_token",
    "expired_at": "2026-01-13T12:00:00Z"
  }
}
```

**60001 ShareNotFound 响应示例**：

```json
{
  "code": 60001,
  "message": "分享不存在",
  "data": {
    "share_id": "sh_invalid",
    "reason": "分享不存在或不属于当前用户"
  }
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "share_id": "sh_abc123",
    "files": [
      {"id": 1, "name": "文档.pdf", "type": "file", "size": 102400}
    ],
    "share_link": "https://disk.example.com/s/abc123",
    "has_password": true,
    "permission": "download",
    "view_count": 10,
    "download_count": 5,
    "created_at": "2026-01-13T10:00:00Z",
    "expires_at": "2026-01-20T10:00:00Z",
    "status": "active"
  }
}
```

---

### 7.4 更新分享设置

**PUT** `/api/share/{share_id}`

更新分享的设置。

#### 实现状态
**已实现**

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| share_id | string | 分享 ID |

#### 请求参数

```json
{
  "expire_days": 30,
  "password": "newpass",
  "permission": "view"
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| expire_days | integer | 否 | 更新有效期（从当前时间计算） |
| password | string | 否 | 新密码，空字符串表示移除密码 |
| permission | string | 否 | 更新权限 |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | share_id 格式错误 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | password 长度不在 4-8 字符之间 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Token 已超过有效期 |
| 404 | 60001 | `ShareNotFound` | 分享不存在 | share_id 不存在或不属于当前用户 |

**10002 ValidationFailed 响应示例**：

```json
{
  "code": 10002,
  "message": "参数校验失败",
  "data": {
    "field": "password",
    "reason": "访问密码长度必须在 4-8 字符之间",
    "invalid_value": "ab"
  }
}
```

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

**40108 TokenExpired 响应示例**：

```json
{
  "code": 40108,
  "message": "令牌已过期",
  "data": {
    "token_type": "access_token",
    "expired_at": "2026-01-13T12:00:00Z"
  }
}
```

**60001 ShareNotFound 响应示例**：

```json
{
  "code": 60001,
  "message": "分享不存在",
  "data": {
    "share_id": "sh_invalid",
    "reason": "分享不存在或不属于当前用户"
  }
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "share_id": "sh_abc123",
    "expires_at": "2026-02-12T10:00:00Z",
    "has_password": true,
    "permission": "view",
    "updated_at": "2026-01-13T11:00:00Z"
  }
}
```

---

### 7.5 取消分享

**DELETE** `/api/share`

取消分享链接。

#### 实现状态
**已实现**

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 请求参数

```json
{
  "share_ids": ["sh_abc123", "sh_def456"]
}
```

#### 批量操作语义

本接口采用**确定性混合结果契约**：无论请求中多少项成功或失败，HTTP 状态码始终返回 `200 OK`，通过响应体中的 `summary` 和 `results` 字段表达每项的处理结果。

此设计避免了通过顶层 404 编码"部分成功"的歧义，使客户端能够：
1. 明确知道请求已被服务端完整处理
2. 精确识别哪些项成功、哪些项失败
3. 获取失败项的具体错误原因

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | share_ids 为空或格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | Authorization 头格式不正确 |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Token 已超过有效期 |

> **注意**：即使请求中所有 `share_ids` 都不存在或无权限，接口仍返回 `200 OK`，通过 `results` 数组中的 `status: "failed"` 表达。

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

**40108 TokenExpired 响应示例**：

```json
{
  "code": 40108,
  "message": "令牌已过期",
  "data": {
    "token_type": "access_token",
    "expired_at": "2026-01-13T12:00:00Z"
  }
}
```

#### 响应体结构

成功响应（HTTP 200）包含以下字段：

| 字段 | 类型 | 说明 |
|------|------|------|
| `summary.total` | integer | 请求取消的分享总数 |
| `summary.succeeded` | integer | 成功取消的数量 |
| `summary.failed` | integer | 取消失败的数量 |
| `results` | array | 每项的处理结果，顺序与请求中的 `share_ids` 一致 |
| `results[].share_id` | string | 分享标识符 |
| `results[].status` | string | `success` 或 `failed` |
| `results[].error` | object | 仅当 `status: "failed"` 时存在 |
| `results[].error.code` | integer | 业务错误码 |
| `results[].error.message` | string | 错误消息 |
| `results[].error.reason` | string | 详细错误原因 |

#### 响应示例 - 全部成功

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "summary": {
      "total": 2,
      "succeeded": 2,
      "failed": 0
    },
    "results": [
      {
        "share_id": "sh_abc123",
        "status": "success"
      },
      {
        "share_id": "sh_def456",
        "status": "success"
      }
    ]
  }
}
```

#### 响应示例 - 部分成功

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "summary": {
      "total": 3,
      "succeeded": 1,
      "failed": 2
    },
    "results": [
      {
        "share_id": "sh_abc123",
        "status": "success"
      },
      {
        "share_id": "sh_invalid1",
        "status": "failed",
        "error": {
          "code": 60001,
          "message": "分享不存在",
          "reason": "分享不存在或不属于当前用户"
        }
      },
      {
        "share_id": "sh_expired",
        "status": "failed",
        "error": {
          "code": 60002,
          "message": "分享已过期",
          "reason": "分享已超过有效期，无法取消"
        }
      }
    ]
  }
}
```

#### 响应示例 - 全部失败

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "summary": {
      "total": 2,
      "succeeded": 0,
      "failed": 2
    },
    "results": [
      {
        "share_id": "sh_invalid1",
        "status": "failed",
        "error": {
          "code": 60001,
          "message": "分享不存在",
          "reason": "分享不存在或不属于当前用户"
        }
      },
      {
        "share_id": "sh_invalid2",
        "status": "failed",
        "error": {
          "code": 60001,
          "message": "分享不存在",
          "reason": "分享不存在或不属于当前用户"
        }
      }
    ]
  }
}
```

---

### 7.6 验证分享访问

**POST** `/api/share/access/{share_id}`

验证分享并获取访问令牌（供访客使用，无需登录）。签发的 Share Token 将当前分享的外部 `share_id` 和 `permission` 固化在 `scope` claim 中；完整结构和操作映射见 9.4.2。

#### 实现状态
**已实现**

此接口无需认证，访客可直接访问。

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| share_id | string | 分享 ID |

#### 请求参数

```json
{
  "password": "abc123"
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| password | string | 否 | 访问密码（如果分享设置了密码） |

#### 公开访问失败统一契约

仅以下场景属于**可计数验证失败**：受密码保护的分享缺失密码、密码为空、密码错误，以及 `share_id` 对应的分享不存在。服务端按“提供的分享标识 + 规范化客户端 IP”计数，前 5 次必须返回完全相同的响应，不得通过状态码、业务码、消息或 `data` 泄露分享是否存在：

```json
{
  "code": 60003,
  "message": "Share access validation failed",
  "data": null
}
```

第 6 次及后续可计数验证失败返回现有的限流响应：HTTP 429、业务码 `10005 TooManyRequests`、消息 `Too many password verification attempts, please try again later`、`data: null`。已过期或已取消的分享继续使用现有语义，不纳入上述统一失败契约，也不计入该失败计数器。

#### 失败计数与窗口语义

- Redis 键固定为 `rate:share_password:{share_code}:{normalized_ip}`；`share_code` 使用请求提供的分享标识，客户端 IP 去除端口后再参与键构造。
- 计数器仅记录可计数验证失败。正确密码访问和无密码分享访问均不增加计数，也不清除已有计数。
- 第一次失败创建固定 900 秒窗口；后续失败只原子递增，不刷新过期时间，不使用独立锁定键或滑动窗口。
- Redis 不可用或计数操作失败时限流逻辑失败开放（fail open），继续执行公开访问验证；若验证仍失败，返回上述统一 HTTP 400 响应。

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | share_id 格式错误 |
| 400 | 60003 | `SharePasswordError` | `Share access validation failed` | 前 5 次缺失/空/错误密码，或分享不存在 |
| 429 | 10005 | `TooManyRequests` | `Too many password verification attempts, please try again later` | 第 6 次及后续可计数验证失败 |
| 400 | 60002 | `ShareExpired` | 分享已过期 | 分享已过期；不属于统一失败契约 |

**60003 SharePasswordError 统一响应示例**：

```json
{
  "code": 60003,
  "message": "Share access validation failed",
  "data": null
}
```

**10005 TooManyRequests 响应示例**：

```json
{
  "code": 10005,
  "message": "Too many password verification attempts, please try again later",
  "data": null
}
```

**60002 ShareExpired 响应示例**：

```json
{
  "code": 60002,
  "message": "分享已过期",
  "data": {
    "share_id": "sh_abc123",
    "expired_at": "2026-01-10T10:00:00Z",
    "reason": "分享已超过有效期"
  }
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "share_token": "st_xyz789...",
    "expires_in": 3600,
    "permission": "download",
    "files": [
      {
        "id": 1,
        "name": "文档.pdf",
        "type": "file",
        "size": 102400
      }
    ]
  }
}
```

---

### 7.7 浏览分享内容

**GET** `/api/share/browse/{share_id}`

浏览分享的文件夹内容（使用分享令牌）。`scope.permission` 为 `view` 或 `download` 的令牌均可浏览。服务端会在每次浏览时复查 `shares.status` 和 `expires_at`，因此分享被取消或过期后，已签发但尚未过期的 `X-Share-Token` 也不能继续浏览。

#### 实现状态
**已实现**

#### 请求头

```
X-Share-Token: <share_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| X-Share-Token | 是 | 分享访问令牌（通过 /api/share/access 获取） |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| share_id | string | 分享 ID |

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| folder_id | integer | 否 | 文件夹 ID（分享内的相对 ID） |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | share_id 或 folder_id 格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 X-Share-Token |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | X-Share-Token 格式无效（非 JWT 或签名损坏） |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Share Token 已超过有效期 |
| 404 | 60001 | `ShareNotFound` | 分享不存在 | share_id 不存在 |
| 400 | 60002 | `ShareExpired` | 分享已过期 | 分享已超过有效期 |
| 404 | 50006 | `FolderNotFound` | 文件夹不存在 | folder_id 不存在于分享中 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

**40107 TokenMalformed 响应示例**：

```json
{
  "code": 40107,
  "message": "令牌格式错误",
  "data": {
    "reason": "X-Share-Token 格式无效（非 JWT 或签名损坏）"
  }
}
```

**40108 TokenExpired 响应示例**：

```json
{
  "code": 40108,
  "message": "令牌已过期",
  "data": {
    "token_type": "share_token",
    "expired_at": "2026-01-13T12:00:00Z"
  }
}
```

**60001 ShareNotFound 响应示例**：

```json
{
  "code": 60001,
  "message": "分享不存在",
  "data": {
    "share_id": "sh_invalid",
    "reason": "分享不存在或已被取消"
  }
}
```

**60002 ShareExpired 响应示例**：

```json
{
  "code": 60002,
  "message": "分享已过期",
  "data": {
    "share_id": "sh_abc123",
    "expired_at": "2026-01-10T10:00:00Z",
    "reason": "分享已超过有效期"
  }
}
```

**50006 FolderNotFound 响应示例**：

```json
{
  "code": 50006,
  "message": "文件夹不存在",
  "data": {
    "folder_id": 99999,
    "reason": "指定的文件夹不存在于分享中"
  }
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "items": [
      {"id": 1, "name": "子文件.txt", "type": "file", "size": 1024}
    ],
    "breadcrumb": [
      {"id": 0, "name": "分享根目录"}
    ]
  }
}
```

---

### 7.8 下载分享文件

**GET** `/api/share/download/{share_id}/{file_id}`

下载分享的文件。下载元数据接口 `GET /api/share/download/{share_id}/{file_id}/info` 和实际内容下载均要求 `scope.permission = "download"`，并在每次请求中复查数据库当前 `permission`、`shares.status` 和 `expires_at`。因此仅查看令牌不能下载；分享权限降级、取消或过期后，已签发但尚未过期的 `X-Share-Token` 也不能继续获取下载元数据或内容。

#### 实现状态
**已实现**

#### 请求头

```
X-Share-Token: <share_token>
Range: bytes=0-1048575 (可选)
```

| Header | 必填 | 说明 |
|--------|------|------|
| X-Share-Token | 是 | 分享访问令牌（通过 /api/share/access 获取） |
| Range | 否 | 断点续传范围 |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| share_id | string | 分享 ID |
| file_id | integer | 文件 ID |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | share_id 或 file_id 格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 X-Share-Token |
| 401 | 40107 | `TokenMalformed` | 令牌格式错误 | X-Share-Token 格式无效（非 JWT 或签名损坏） |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Share Token 已超过有效期 |
| 404 | 60001 | `ShareNotFound` | 分享不存在 | share_id 不存在 |
| 400 | 60002 | `ShareExpired` | 分享已过期 | 分享已超过有效期 |
| 403 | 60004 | `ShareAccessDenied` | 无权限访问 | 分享设置为仅查看，不允许下载 |
| 404 | 50005 | `FileNotFound` | 文件不存在 | file_id 不存在于分享中 |
| 500 | 50011 | `FileReadError` | 文件读取失败 | 文件元数据存在，但最终对象缺失、长度不一致或无法打开读取流 |

**40106 TokenMissing 响应示例**：

```json
{
  "code": 40106,
  "message": "未提供令牌",
  "data": null
}
```

**40107 TokenMalformed 响应示例**：

```json
{
  "code": 40107,
  "message": "令牌格式错误",
  "data": {
    "reason": "X-Share-Token 格式无效（非 JWT 或签名损坏）"
  }
}
```

**40108 TokenExpired 响应示例**：

```json
{
  "code": 40108,
  "message": "令牌已过期",
  "data": {
    "token_type": "share_token",
    "expired_at": "2026-01-13T12:00:00Z"
  }
}
```

**60001 ShareNotFound 响应示例**：

```json
{
  "code": 60001,
  "message": "分享不存在",
  "data": {
    "share_id": "sh_invalid",
    "reason": "分享不存在或已被取消"
  }
}
```

**60002 ShareExpired 响应示例**：

```json
{
  "code": 60002,
  "message": "分享已过期",
  "data": {
    "share_id": "sh_abc123",
    "expired_at": "2026-01-10T10:00:00Z",
    "reason": "分享已超过有效期"
  }
}
```

**60004 ShareAccessDenied 响应示例**：

```json
{
  "code": 60004,
  "message": "无权限访问",
  "data": {
    "share_id": "sh_abc123",
    "file_id": 1,
    "permission": "view",
    "reason": "分享设置为仅查看，不允许下载"
  }
}
```

**50005 FileNotFound 响应示例**：

```json
{
  "code": 50005,
  "message": "文件不存在",
  "data": {
    "file_id": 99999,
    "reason": "指定的文件不存在于分享中"
  }
}
```

#### Range 请求语义

本接口支持 HTTP Range 请求，遵循 [RFC 7233](https://datatracker.ietf.org/doc/html/rfc7233) 规范，用于断点续传和分片下载场景。

| 场景 | 请求头 | HTTP 状态码 | 说明 |
|------|--------|------------|------|
| **完整下载** | 无 Range | `200 OK` | 返回完整文件内容 |
| **范围下载** | `Range: bytes=0-1023` | `206 Partial Content` | 返回指定字节范围的内容 |
| **范围无效** | `Range: bytes=999999-`（超出文件大小） | `416 Range Not Satisfiable` | 范围无法满足，响应体包含错误信息 |

**Range 请求格式说明**：
- 仅支持 `bytes` 单位
- 格式：`bytes=<start>-<end>`（end 可选，表示到文件末尾）
- 示例：`Range: bytes=0-1048575`（下载前 1MB）

#### 响应状态码矩阵

| HTTP 状态码 | 触发条件 | 响应体 |
|------------|----------|--------|
| `200 OK` | 无 Range 请求，返回完整文件 | 文件二进制数据流 |
| `206 Partial Content` | 有效 Range 请求 | 请求范围的字节片段 |
| `416 Range Not Satisfiable` | Range 范围无效或超出文件大小 | JSON 错误响应（见下方示例） |
| `500 Internal Server Error` | 响应开始前发现最终对象缺失、长度不符或无法打开 | JSON 错误响应，业务码 `50011` |

响应开始后的上游短读与所有者下载采用同一合同：连接以未满足声明 `Content-Length` 的不完整传输
结束，服务端持久化 `final_blob_read_interrupted` 对账 finding，客户端通过 Range 重试。

#### 响应头

| Header | 必须返回 | 说明 |
|--------|---------|------|
| `Content-Type` | 是 | 文件 MIME 类型（如 `application/pdf`） |
| `Content-Length` | 是 | 返回数据的字节长度 |
| `Content-Disposition` | 是 | `attachment; filename="<文件名>"`，建议使用 RFC 5987 编码非 ASCII 文件名 |
| `Accept-Ranges` | 是 | 固定值 `bytes`，表示支持范围请求 |
| `Content-Range` | 206 必须 | 范围信息，格式：`bytes <start>-<end>/<total>` |
| `ETag` | 建议 | 文件版本标识（如文件哈希），用于条件请求 |
| `Last-Modified` | 建议 | 文件最后修改时间，用于条件请求 |

**200 OK 响应头示例**：

```http
HTTP/1.1 200 OK
Content-Type: application/pdf
Content-Length: 104857600
Content-Disposition: attachment; filename="document.pdf"
Accept-Ranges: bytes
ETag: "d41d8cd98f00b204e9800998ecf8427e"
Last-Modified: Mon, 13 Jan 2026 10:00:00 GMT
```

**206 Partial Content 响应头示例**：

```http
HTTP/1.1 206 Partial Content
Content-Type: application/pdf
Content-Length: 1048576
Content-Range: bytes 0-1048575/104857600
Content-Disposition: attachment; filename="document.pdf"
Accept-Ranges: bytes
ETag: "d41d8cd98f00b204e9800998ecf8427e"
```

**416 Range Not Satisfiable 响应**：

当请求的 Range 范围无法满足时（如起始位置超出文件大小），返回 `416` 状态码：

```http
HTTP/1.1 416 Range Not Satisfiable
Content-Type: application/json
Content-Range: bytes */104857600

{
  "code": 10002,
  "message": "请求范围无效",
  "data": {
    "file_size": 104857600,
    "requested_range": "bytes=999999000-",
    "reason": "请求的起始位置超出文件大小"
  }
}
```

#### 条件请求（可选）

服务端建议支持以下条件请求头，用于优化缓存和断点续传验证：

| Header | 说明 |
|--------|------|
| `If-Range` | 值为 `ETag` 或 `Last-Modified`；若匹配则返回 206，否则返回 200 完整内容 |
| `If-Match` | 值为 `ETag`；若不匹配则返回 412 Precondition Failed |
| `If-None-Match` | 值为 `ETag`；若匹配则返回 304 Not Modified |

**If-Range 请求示例**：

```http
GET /api/share/download/sh_abc123/1 HTTP/1.1
X-Share-Token: st_xyz789...
Range: bytes=1048576-
If-Range: "d41d8cd98f00b204e9800998ecf8427e"
```

- 若 ETag 匹配：返回 `206 Partial Content` 及后续内容
- 若 ETag 不匹配（文件已变更）：返回 `200 OK` 及完整文件

#### 响应体

成功时返回文件二进制数据流。

---

## 8. 系统接口

### 8.1 健康检查

**GET** `/api/health/live`

**GET** `/api/health/ready`

**GET** `/api/health`（兼容别名，语义与 `/api/health/ready` 完全相同）

#### 实现状态
**✅ 已实现**

三个端点均无需认证。liveness 只证明进程事件循环仍可响应，不访问 PostgreSQL、Redis
或对象存储；进程进入 drain 后、真正退出前仍返回 200。readiness 判断当前角色能否接受新工作：

- 所有角色必须完成启动初始化、未进入 drain，并能访问 PostgreSQL、staging 与 final 存储；
- `api` 还必须通过 Redis PING；
- `worker` 还必须读取 `storage_jobs`；启用任务认领时，认领运行时必须仍接受新任务；显式关闭认领的观察模式保持 `worker_accepting=false`，但依赖健康时 readiness 为 200；
- `all` 同时满足 `api` 与有效 Worker 模式的全部条件。

对象存储探针执行每个前缀最多一项的有界只读 inventory 请求，不创建或删除探针对象；写权限由
启动校验及部署验收测试证明。依赖失败只返回固定消息，禁止回显异常文本、连接地址、用户名、
bucket、对象 key 或凭据。

#### 响应字段

| 字段 | 类型 | 说明 |
|------|------|------|
| overall_status | string | 整体状态：`healthy` / `degraded` / `unhealthy` |
| role | string | 当前进程角色：`api` / `worker` / `all` |
| instance_id | string | 当前进程实例 ID |
| initialized | boolean | 当前角色的启动初始化是否完成 |
| draining | boolean | 是否正在优雅退出 |
| worker_claiming_enabled | boolean | 当前进程是否具备 Worker 角色且启动配置允许认领；API 固定为 false，观察 Worker 为 false |
| worker_accepting | boolean | Worker 认领运行时当前是否接受新任务；观察、API 和 drain 状态为 false |
| upload_task_creation_enabled | boolean | 当前进程启动时冻结的新上传任务创建开关；回滚截止后必须为 false |
| business_requests_inflight | integer | 当前已接受但尚未完成的业务请求数；健康与指标路径不计入 |
| version | string | 系统版本 |
| uptime | integer | 运行时间（秒） |
| timestamp | string | ISO 8601 时间戳 |
| components | object | 各组件状态 |
| components.database | object | 数据库状态 |
| components.redis | object | Redis 状态 |
| components.staging_storage | object | 上传暂存存储状态（readiness） |
| components.final_storage | object | 最终 Blob 存储状态（readiness） |
| components.storage_jobs | object | 持久任务表状态（worker/all readiness） |
| components.*.status | string | 组件状态：`healthy` / `unhealthy` |
| components.*.message | string | 错误信息（可选） |
| components.*.latency_ms | integer | 响应延迟（毫秒） |

#### HTTP 状态码

| 状态码 | 说明 |
|--------|------|
| 200 | liveness 可响应，或 readiness 全部条件满足 |
| 503 | readiness 尚未初始化、正在 drain 或任一必需依赖不健康 |

#### readiness 成功响应示例（200，api）

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "overall_status": "healthy",
    "role": "api",
    "instance_id": "disk-api-1",
    "initialized": true,
    "draining": false,
    "worker_claiming_enabled": false,
    "worker_accepting": false,
    "upload_task_creation_enabled": false,
    "business_requests_inflight": 0,
    "version": "1.0.0",
    "uptime": 86400,
    "timestamp": "2026-02-18T12:30:00Z",
    "components": {
      "database": {
        "status": "healthy",
        "latency_ms": 5
      },
      "redis": {
        "status": "healthy",
        "latency_ms": 2
      },
      "staging_storage": {
        "status": "healthy",
        "latency_ms": 8
      },
      "final_storage": {
        "status": "healthy",
        "latency_ms": 7
      }
    }
  }
}
```

#### readiness 不健康响应示例（503）

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "overall_status": "unhealthy",
    "role": "worker",
    "instance_id": "disk-worker-2",
    "initialized": true,
    "draining": false,
    "worker_claiming_enabled": true,
    "worker_accepting": true,
    "upload_task_creation_enabled": true,
    "business_requests_inflight": 0,
    "version": "1.0.0",
    "uptime": 86400,
    "timestamp": "2026-02-18T12:30:00Z",
    "components": {
      "database": {
        "status": "unhealthy",
        "message": "Database check failed",
        "latency_ms": 0
      },
      "storage_jobs": {
        "status": "unhealthy",
        "message": "Storage job queue check failed",
        "latency_ms": 0
      },
      "staging_storage": {
        "status": "healthy",
        "latency_ms": 2
      },
      "final_storage": {
        "status": "healthy",
        "latency_ms": 2
      }
    }
  }
}
```

---

### 8.2 系统信息

**GET** `/api/system/info`

#### 实现状态
**✅ 已实现**

获取系统信息（需要认证）。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 响应字段

| 字段 | 类型 | 说明 |
|------|------|------|
| version | string | 系统版本 |
| drogon_version | string | Drogon 框架版本 |
| build_time | string | 构建时间 |
| uptime | integer | 运行时间（秒） |
| connections | object | 连接信息 |
| connections.current | integer | 当前连接数 |
| connections.peak | integer | 峰值连接数 |
| storage | object | 存储统计 |
| storage.total_users | integer | 用户总数 |
| storage.total_files | integer | 文件总数 |
| storage.total_folders | integer | 文件夹总数 |
| storage.total_size | integer | 文件总大小（字节） |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Token 已超过有效期 |

#### 成功响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "version": "1.0.0",
    "drogon_version": "1.9.11",
    "build_time": "Feb 18 2026 20:30:00",
    "uptime": 86400,
    "connections": {
      "current": 1,
      "peak": 10
    },
    "storage": {
      "total_users": 100,
      "total_files": 5000,
      "total_folders": 200,
      "total_size": 549755813888
    }
  }
}
```

---

### 8.3 获取操作日志

**GET** `/api/logs`

#### 实现状态
**已实现**

获取当前用户的操作日志列表。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌 |

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| page | integer | 否 | 页码，默认 1 |
| page_size | integer | 否 | 每页数量，默认 20，最大 100 |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | page 或 page_size 格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |

#### 响应字段

| 字段 | 类型 | 说明 |
|------|------|------|
| items | array | 日志条目数组 |
| items[].id | integer | 日志 ID |
| items[].action | string | 操作类型（login, logout, upload, download, delete, rename, move, copy, share, restore） |
| items[].target_type | string | 目标类型（file, folder, share, user） |
| items[].target_id | integer | 目标 ID（可选） |
| items[].target_name | string | 目标名称（可选） |
| items[].details | string | 操作详情 JSON（可选） |
| items[].ip_address | string | 客户端 IP 地址 |
| items[].created_at | string | 操作时间 |
| total | integer | 总记录数 |
| page | integer | 当前页码 |
| page_size | integer | 每页数量 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "items": [
      {
        "id": 1,
        "action": "upload",
        "target_type": "file",
        "target_id": 123,
        "target_name": "document.pdf",
        "ip_address": "192.168.1.100",
        "created_at": "2026-02-18T12:30:00Z"
      },
      {
        "id": 2,
        "action": "download",
        "target_type": "file",
        "target_id": 456,
        "target_name": "report.docx",
        "ip_address": "192.168.1.100",
        "created_at": "2026-02-18T12:25:00Z"
      }
    ],
    "total": 50,
    "page": 1,
    "page_size": 20
  }
}
```

---

## 9. 接口安全

### 9.1 JWT 令牌结构

**Access Token Payload:**

```json
{
  "sub": "1",
  "username": "john_doe",
  "type": "access",
  "iat": 1705132800,
  "exp": 1705140000
}
```

**Refresh Token Payload:**

```json
{
  "sub": "1",
  "type": "refresh",
  "jti": "unique-token-id",
  "iat": 1705132800,
  "exp": 1705737600
}
```

### 9.2 访问频率限制

| 接口类型 | 限制规则 |
|----------|----------|
| 认证接口 | 10 次/分钟/IP |
| 普通接口 | 100 次/分钟/用户 |
| 上传接口 | 60 次/分钟/用户 |
| 下载接口 | 无限制（带宽限制） |
| 分享访问 | 30 次/分钟/IP |

### 9.3 请求签名（可选）

对于高安全要求的场景，可启用请求签名验证：

```
X-Timestamp: 1705132800
X-Nonce: random-string
X-Signature: HMAC-SHA256(timestamp + nonce + body, secret)
```

### 9.4 分享访问安全控制

分享功能涉及公开/半公开资源访问，需要额外的安全防护措施。

#### 9.4.1 密码暴力破解防护

对于公开分享访问验证，服务端必须实施以下防护措施：

| 防护措施 | 实现要求 | 触发条件 |
|---------|---------|---------|
| **失败计数** | Redis 计数器 `rate:share_password:{share_code}:{normalized_ip}` | 缺失/空/错误密码，或分享不存在 |
| **失败限制** | 前 5 次返回统一验证失败；第 6 次及后续返回 HTTP 429 / `10005` / `Too many password verification attempts, please try again later` / `data: null` | 同一规范化 IP + 同一请求分享标识 |
| **固定窗口** | 首次失败设置 900 秒 TTL，后续失败不得刷新 | 首次可计数验证失败 |
| **成功语义** | 正确密码及无密码访问不增加、不清除计数 | 公开访问验证成功 |
| **Redis 故障** | 限流计数失败开放，继续访问验证 | Redis 不可用或计数操作失败 |

**Redis 键设计**：
```
rate:share_password:{share_code}:{normalized_ip}  -> 可计数验证失败次数 (int, TTL=900s)
```

不使用其他键前缀、独立锁定键或“最后一次尝试后重新计时”的滑动窗口。`share_code` 为请求提供的分享标识，即使分享不存在也使用该值计数；`normalized_ip` 为去除端口后的客户端 IP。

**前 5 次失败响应**：

```json
{
  "code": 60003,
  "message": "Share access validation failed",
  "data": null
}
```

**第 6 次及后续失败响应**：

```json
{
  "code": 10005,
  "message": "Too many password verification attempts, please try again later",
  "data": null
}
```

已过期和已取消的分享继续使用现有错误语义，位于统一失败契约和该失败计数器之外。

#### 9.4.2 Share Token 防护

Share Token（通过 `/api/share/access` 获取）需要以下安全措施：

| 防护措施 | 实现要求 | 说明 |
|---------|---------|------|
| **短有效期** | 默认 1 小时（3600 秒） | 限制被盗用后的攻击窗口 |
| **单次使用/绑定** | 建议绑定 Client IP 或 User-Agent | 可选增强措施 |
| **撤销机制** | 分享取消时立即使所有 token 失效 | 通过 Redis 黑名单或 DB 状态检查 |
| **类型标识** | JWT `type` claim = `share` | 区分于 access_token/refresh_token |
| **作用域限定** | JWT `scope` claim 包含外部 `share_id` 和签发时的 `permission` | 限定 token 只能访问指定分享且不能在签发后扩权 |

**Scope JSON 合同**：

- `scope` 必须是 JSON object，且必须包含字符串字段 `share_id` 和 `permission`。
- `scope.share_id` 是 API 使用的外部分享标识（即 `shares.share_code`），必须与顶层 `share_code` claim 完全一致。
- `scope.permission` 只允许 `view` 或 `download`，取签发时数据库中的分享权限。
- 缺失 `scope`、类型错误、字段缺失/类型错误、非法权限值或 `scope.share_id` 与顶层 `share_code` 不一致时，令牌验证返回 HTTP 401 / `40107 TokenMalformed`。
- Scope 是令牌能力上限，数据库当前分享记录是实时授权下限：操作必须同时满足 token scope 和当前 `shares.permission`。把分享从 `download` 改为 `view` 会立即禁止旧下载令牌；把分享从 `view` 改为 `download` 不会让旧查看令牌获得下载或保存能力，访客必须重新访问分享以取得新令牌。

**权限到访客操作映射**：

| 访客操作 | HTTP/服务路径 | 允许的 `scope.permission` | 每次操作的数据库复查 |
|---------|---------------|---------------------------|------------------------|
| 浏览分享 | `GET /api/share/browse/{share_id}` / `Browse` | `view`, `download` | `status`、`expires_at` |
| 获取下载元数据 | `GET /api/share/download/{share_id}/{file_id}/info` / `GetDownloadInfo` | `download` | `permission`、`status`、`expires_at` |
| 下载文件内容 | `GET /api/share/download/{share_id}/{file_id}` / `GetDownloadInfo` 后构造响应 | `download` | `permission`、`status`、`expires_at` |
| 保存到网盘 | `POST /api/share/save/{share_id}` / `SaveToDrive` | `download`，并同时要求有效 owner Access Token | `permission`、`status`、`expires_at` |

**Share Token Payload 示例**：

```json
{
  "sub": "12345",
  "share_code": "sh_abc123",
  "type": "share",
  "scope": {
    "share_id": "sh_abc123",
    "permission": "download"
  },
  "iat": 1705132800,
  "exp": 1705136400,
  "jti": "unique-token-id"
}
```

`sub` 是服务端用于查询的内部 `shares.id` 十进制字符串；外部授权绑定以 `share_code` 和 `scope.share_id` 为准，二者必须一致。

**Token 撤销检查**：

服务端分两层检查 Share Token：

1. `ShareAuthFilter` 验证 JWT 签名、issuer、`type`、过期时间、JTI、scope 结构/绑定以及可选的单 token Redis 黑名单。
2. 通过 JWT 验证后，每个访客业务路径仍重新读取分享记录并检查 `shares.status`、`shares.expires_at`；下载元数据、内容下载和保存到网盘还必须检查数据库当前 `permission = 'download'`。

各 API 实例只正缓存已撤销的 token hash，不缓存“未撤销”结果；缓存未命中必须查询共享 Redis。Redis 校验失败返回 `70002` / HTTP 500，且不得把错误降级为“未撤销”。

分享令牌签发返回自包含 JWT，不向 Redis 写入 `share_token:{share_code}:{token_hash}` allowlist，验证也不依赖该 namespace。这与可选的单 token blacklist 是两个不同概念，不得为死 allowlist builder 创造或保留 Redis 状态。

取消分享采用分享记录状态撤销语义：取消操作把 `shares.status` 更新为 cancelled 后，所有已签发 token 在下一次访客操作的数据库状态检查中立即失效，不要求枚举 token，也不要求把每个 token hash 写入 Redis。Redis `share_token_blacklist:{token_hash}` 仅表示某一个具体 token 已撤销，不能替代业务路径的实时分享状态检查；当前 HTTP/Service 业务路径不暴露按原始 token 写入该键的通用命令，过滤器只消费受信运维或兼容流程已经持久化的精确 hash 状态。删除无调用方的 `TokenService::RevokeShareToken` 不得删除该读取契约、正缓存或 fail-closed 行为。

**撤销响应语义**：

```json
{
  "code": 60002,
  "message": "Share expired",
  "data": null
}
```

分享被取消或 `expires_at` 到期时返回 HTTP 400 / `60002 ShareExpired`；只有具体 token hash 命中 Redis 黑名单时返回 HTTP 401 / `40111 TokenRevoked`。

#### 9.4.3 速率限制强化

分享访问、浏览和下载使用相互独立的 Redis 固定窗口。第一次请求创建计数器并设置配置的 TTL，后续原子递增不得刷新过期时间。

| 限流器 | 覆盖路由 | 默认限制 | 键身份 | 必须的过滤顺序 |
|--------|----------|----------|--------|----------------|
| **分享访问** | `POST /api/share/access/{share_id}` | 30 次 / 60 秒 | 规范化客户端 IP | 路由级访问限流器；不要求 owner JWT 或 Share Token |
| **分享浏览** | `GET /api/share/browse/{share_id}` | 60 次 / 60 秒 | 已验证 Share Token 的 JTI | `ShareAuthFilter`，然后路由级分享操作限流器 |
| **分享下载** | 下载元数据、内容和保存到网盘 | 10 次 / 60 秒 | 已验证 Share Token 的 JTI | `ShareAuthFilter`，然后路由级分享操作限流器 |

分享下载桶由以下请求共同消耗：

- `GET /api/share/download/{share_id}/{file_id}/info`
- `GET /api/share/download/{share_id}/{file_id}`
- `POST /api/share/save/{share_id}`
- 每一个二进制下载 HTTP 请求，包括首次请求、Range 续传和自动重试；服务端不把多个请求合并为一次逻辑下载计费。

**认证和计数顺序**：

- `ShareAuthFilter` 只有在签名、过期、Redis 单 token 撤销和操作 scope 全部验证通过后，才把 JTI 作为 `share_token_jti` 请求属性交给后续限流器。缺失、格式错误、过期、撤销或 scope 不足的 token 保留原认证/授权响应，不消耗 browse/download 桶。
- 保存到网盘不是 owner JWT 公开豁免路径。全局 owner JWT、路由级 Share Token 认证和 download 桶依次通过后才进入业务处理；任一认证失败均不消耗 download 桶。
- 分享取消、到期和数据库当前权限等实时状态仍由业务服务复查。结构和 scope 合法的 token 可能先消耗一次操作请求，再收到实时状态业务拒绝；本次限流变更不迁移数据库授权边界。

**访问桶和密码失败桶**：

通用访问桶统计所有 `/access` 请求，包括成功、无密码和被拒绝的请求。它与 `rate:share_password:{share_code}:{normalized_ip}` 失败验证桶完全独立：可计数密码失败同时消耗两个桶，但任一桶都不清除或消耗另一个桶。密码失败桶继续使用前 5 次 HTTP 400 / `60003`、第 6 次及后续 HTTP 429 / `10005` 的 900 秒合同。

**Redis 键和运行配置**：

| 限流器 | Redis 键 | limit 配置 | window 配置 |
|--------|-----------|------------|---------------|
| 分享访问 | `rate:share_access:{normalized_ip}:{window}` | `share_access_rate_limit_per_minute`（默认 30） | `share_access_rate_limit_window_seconds`（默认 60） |
| 分享浏览 | `rate:share_browse:{jti}:{window}` | `share_browse_rate_limit_per_minute`（默认 60） | `share_browse_rate_limit_window_seconds`（默认 60） |
| 分享下载 | `rate:share_download:{jti}:{window}` | `share_download_rate_limit_per_minute`（默认 10） | `share_download_rate_limit_window_seconds`（默认 60） |

配置缺失、为零、为负数或不能作为正整数使用时回退到本表默认值。旧 `share_public_rate_limit_per_minute`、`share_public_rate_limit_window_seconds` 和 `rate:share_public:*` 不提供运行时别名或兼容读取。

**操作限流的 429 响应**：

```json
{
  "code": 10005,
  "message": "Too many requests",
  "data": null
}
```

HTTP 状态为 429，并同时返回 `X-RateLimit-Limit`、`X-RateLimit-Remaining: 0`、`X-RateLimit-Reset` 和 `Retry-After`。Redis 计数失败时记录不含凭据的操作类型和诊断信息，并 fail open 继续底层业务请求，不得仅因限流状态不可用而返回 429 或 500。

密码失败桶仍保留专用消息 `Too many password verification attempts, please try again later`；它不替换上述三类操作桶的标准响应。

**客户端兼容性说明**：

浏览、下载和保存新增了既有 HTTP 429 / `10005` 错误面，但路由、请求头、成功响应和错误 envelope 均未改变。Web 已识别通用 `10005`，Desktop `ErrorAdapter` 已将其映射为 `RateLimited` / `wait_and_retry`，其余二进制请求也按非 2xx 失败处理，因此本后端变更不要求同步客户端实现。客户端应优先使用 `Retry-After` 或 `X-RateLimit-Reset` 安排重试；更细致的倒计时或自动重试属于后续客户端体验增强。

#### 9.4.4 审计日志

以下分享相关事件必须记录到 `operation_logs` 表：

| 事件类型 | `action` 值 | `user_id` | `details` 稳定字段 |
|---------|------------|-----------|-------------------|
| **分享创建** | `share_create` | 所有者 ID | `share_code`, `file_ids`, `folder_ids`, `permission`, `expires_at`, `success`, `result` |
| **分享访问** | `share_access` | `NULL` | `share_code`, `success`, `result` |
| **公开验证失败** | `share_pwd_fail` | `NULL` | `share_code`, `attempt_count`, `counter_available`, `rate_limited`, `success`, `result` |
| **文件下载** | `share_download` | `NULL` | `share_code`, `file_id`, `bytes`, `http_status`, `success`, `result` |
| **分享取消** | `share_cancel` | 取消操作者 ID | `share_code`, `cancelled_by`, `success`, `result` |

所有事件固定使用 `target_type='share'`、`target_name=share_code`。能够解析分享时，`target_id` 必须是数据库内部 `shares.id`（BIGINT）；不存在的公开分享标识或批量取消项无法解析时，`target_id` 为 `NULL`，不得伪造内部 ID。外部标识 `share_code` 同时保存在 `details` 中用于追溯。

`ip_address` 和 `user_agent` 使用表的普通列，不在 `details` 中重复。`share_access` 对成功和拒绝结果各写一条；公开密码/不存在分享验证失败还额外写一条 `share_pwd_fail`。批量取消按请求中的每一项写一条 `share_cancel`，包括成功、找不到、越权、已取消和内部失败结果。

`share_download.bytes` 表示服务端为 HTTP 200/206 响应选择的内容负载长度；拒绝、找不到内容和 416 响应记录 0。它不是客户端断开连接后可证明的实际接收字节数。`GET .../info` 元数据请求不属于下载事件。

**审计日志字段要求**（对应 `sql/init.sql` 定义）：

```sql
-- operation_logs 表关键字段
user_id     BIGINT NULL      -- 操作者 ID；公开访客为 NULL
action      VARCHAR(32)      -- 操作类型
target_type VARCHAR(32)      -- 目标类型（如 'share', 'file', 'folder'）
target_id   BIGINT           -- 目标内部 ID（shares.id，非 share_code）
target_name VARCHAR(255)     -- 目标名称（可选，如分享的文件名）
details     JSONB            -- 操作详情（含 share_code 等外部标识）
ip_address  VARCHAR(45)      -- 客户端 IP
user_agent  VARCHAR(512)     -- 客户端标识
created_at  TIMESTAMP        -- 操作时间
```

分享审计统一由 `ShareAuditService` 写入，controller 只向业务 service 传递经过解析的 IP 和 User-Agent，不得包含审计 SQL。审计写入采用 **fail-open、无自动重试** 策略：创建、访问、验证失败、下载和逐项取消均先保持既有业务结果；审计写入失败时记录包含 action、内部分享 ID（若有）和 `share_code` 的错误日志，但不得把数据库错误改写成业务失败。无自动重试可避免未设置幂等键时重复记录；后续若引入可靠重试，必须先增加稳定事件 ID 和唯一约束。

审计字段和应用日志严禁保存访问密码、密码哈希、原始 Share Token、Authorization/X-Share-Token 请求头或其他可重放凭据。User-Agent 最多保留 512 个字符。公开访问、验证失败和下载的 `user_id` 必须为 `NULL`，不得归到分享所有者名下。

#### 9.4.5 安全检查清单

实现分享功能时，必须确保以下检查项全部通过：

- [x] 密码验证失败计数器使用 Redis 原子操作，且仅统计失败验证（证据：`ShareService::HandleFailedShareAccess`、`RedisService::IncrWithExpire`、`RedisServiceRuntimeTest`、`SharePasswordProtectionIntegration`）
- [x] Share Token 有效期不超过 1 小时（证据：`TokenService::GetShareTokenExpireSeconds()` 固定返回 3600，`TokenServiceShareTest.GetShareTokenExpireSecondsReturns3600`）
- [x] Share Token 包含 `type: "share"` 和合法 `scope` claim（证据：`TokenServiceShareTest.GenerateShareTokenValidInputCorrectClaims` 及 scope 正向/负向测试）
- [x] Share Token 验证通过后的所有访客操作检查分享状态和有效期（证据：`ShareTokenSecurityIntegration` 覆盖 browse、download metadata、content download、save-to-drive 的取消/过期旧 token）
- [x] 分享取消时使所有相关 Share Token 失效（证据：`ShareTokenSecurityIntegration` 使用同一旧 token 验证 DB 状态撤销，不依赖逐 token Redis 黑名单）
- [x] 敏感操作有独立的速率限制（证据：`ShareRateLimitIntegration` 的十项 `SHARE-RATE-*` 证据全部通过；access 为规范化 IP 独立桶，browse/download 为已验证 JTI 独立桶，download 元数据/内容/Range/重试/save 共用下载桶；聚焦测试 55/55、完整后端 CTest 1,179 个启用测试通过）
- [x] 关键事件写入审计日志（证据：`ShareAuditService`、`ShareAuditServiceTest`、`ShareAuditIntegration` 覆盖 `share_create`、`share_access`、`share_pwd_fail`、`share_download`、逐项 `share_cancel` 及 fail-open 政策）
- [x] 密码错误响应不泄露分享是否存在的信息（证据：`SharePasswordProtectionIntegration` 验证缺失密码、错误密码和不存在分享均统一返回 HTTP 400 / `60003`）

---

## 10. 管理员接口

### 10.0 分布式存储任务运维接口

以下接口与本章其他接口一样，必须经过 `AdminAuthFilter` 和管理员限流。响应不得包含凭据、令牌、文件正文或带签名 URL；`payload` 只允许返回 `storage_jobs` 中经过任务合同约束的非敏感 JSON。

#### 查询任务

**GET** `/api/admin/storage-jobs`

查询参数：`status` 可选值为 `pending/running/retry/succeeded/dead_letter`，`job_type` 为可选精确匹配，`page` 默认 1，`page_size` 默认 20 且最大 100。默认只查询 `dead_letter`，避免运维页面无意扫描完整历史表。

响应 `data.items[]` 包含 `id/job_type/aggregate_id/dedupe_key/status/attempts/max_attempts/available_at/locked_by/locked_until/last_error/created_at/updated_at/completed_at`，并返回标准 `pagination`。列表不返回 `payload`。

**GET** `/api/admin/storage-jobs/{id}`

返回上述字段及 `payload`。ID 必须是正整数；任务不存在返回 HTTP 404 / `10003 ResourceNotFound`。

#### 重放死信

**POST** `/api/admin/storage-jobs/{id}/replay`

```json
{
  "dry_run": true,
  "confirm_job_id": 42,
  "reason": "dependency recovered"
}
```

- `dry_run` 默认为 `true`。dry-run 只返回当前任务和 `eligible`，不修改任务、不写审计。
- 真正重放必须显式传 `dry_run=false`，且 `confirm_job_id` 与路径 ID 完全相同；`reason` 去除首尾空白后必须为 1-256 个字符。
- 只有 `DeadLetter -> Pending` 合法；原子重置 `attempts/available_at/lease/last_error/completed_at`。
- 状态变化与 `operation_logs` 中的 `admin.storage_job.replay` 审计必须在同一 PostgreSQL 事务提交。并发重放只有一个成功，其他请求返回 HTTP 409 / `10004 ResourceConflict`。

#### 上传会话诊断

**GET** `/api/admin/uploads/{upload_id}/diagnostics`

该端点只读，必须经过 `AdminAuthFilter` 和管理员限流。`upload_id` 限 1-64 个
`[A-Za-z0-9._:-]` 字符；任务不存在返回 HTTP 404 / `10003 ResourceNotFound`。查询参数：

| 参数 | 默认 | 范围 | 语义 |
|---|---:|---:|---|
| `chunk_page` | 1 | >= 1 | 分片页码 |
| `chunk_page_size` | 20 | 1-100 | 本次读取并 HEAD 的最大分片数 |
| `job_page` | 1 | >= 1 | 关联存储任务页码 |
| `job_page_size` | 20 | 1-100 | 关联存储任务页大小 |

`data.task` 返回上传状态、`state_version`、预留字节、staging 后端/前缀、完成尝试与最后错误码；
`lease` 在无租约时为 `null`，否则包含 `owner/expires_at/expired`。`data.chunks[]` 返回
PostgreSQL 中的 `chunk_index/size_bytes/hash_md5/object_key/etag/uploaded_at`，以及针对该权威描述符执行的
`object_head`：

```json
{
  "status": "present",
  "size_bytes": 5242880,
  "etag": "\"example-etag\"",
  "matches_record": true,
  "error_code": null
}
```

`object_head.status` 只有 `present/missing/error`。`missing` 时 `matches_record=false`；存储调用失败或 DB
描述符不能安全定位对象时为 `error`，只返回稳定业务 `error_code`，其他 HEAD 和数据库诊断仍正常返回。
local 只比较大小；S3 在 DB 大小与 ETag 均完整时一并比较，元数据不完整时
`matches_record=null`。诊断不读取对象正文，也不修复、重试或续租。

`data.related_jobs` 使用任务列表的 `items/pagination` 形状且不返回 payload，关联范围仅包含：

- `aggregate_id` 为该 `upload_id` 的 `staging_cleanup`；
- S3 multipart key 位于该会话 staging 前缀精确边界内的 `multipart_abort`；
- `upload_staging_mismatch` finding 中稳定 `scan_id` 指向的 `storage_reconcile`。

关联任务的 `last_error` 固定为 `null`；需要查看任务错误与合同 payload 时，再使用精确任务 ID 调用任务详情接口。

响应不包含凭据、令牌、签名 URL、对象正文或底层异常文本。`object_key`和租约 owner
只在该管理员响应中返回，不写入指标标签或通用请求日志。

#### 解除上传完成租约

**POST** `/api/admin/uploads/{upload_id}/lease/release`

```json
{
  "dry_run": true,
  "confirm_upload_id": "0198f5f4-95ae-7c74-aea4-6f6e1c12e4bb",
  "expected_state_version": 7,
  "expected_lease_owner": "api-a:complete:0198f5f4",
  "reason": "owning instance has been terminated"
}
```

- `dry_run` 默认为 `true`，只返回当前 `Finalizing` 状态、owner、数据库租约截止、版本和 `eligible`，不写审计；只有尚未自然到期的匹配租约才具备解除资格。
- 真正执行必须显式传 `dry_run=false`，且 `confirm_upload_id` 与路径完全相同；`expected_state_version` 和 `expected_lease_owner` 必须与当前行完全相同，`reason` 去除首尾空白后为 1-256 个字符。
- 条件更新只命中 `Finalizing + expected owner + expected version + lease_expires_at > NOW()`，将 `lease_expires_at` 写为 PostgreSQL `NOW()` 并递增 `state_version`。状态仍为 `Finalizing`，owner 保留用于诊断；旧 owner 因版本和截止时间双重门禁不能续租或提交，后续 complete 仍通过既有过期接管 CAS 恢复。已经自然到期或已解除的租约直接由正常 complete 接管，重复解除返回 HTTP 409。
- 响应包含 `upload_id/dry_run/eligible/released/status/state_version/lease_owner/lease_expires_at/lease_expired`。实际更新与 `admin.upload.lease_release` 审计同事务提交；条件竞争失败返回 HTTP 409。

#### 重建上传清理任务

**POST** `/api/admin/uploads/{upload_id}/cleanup/rebuild`

```json
{
  "dry_run": true,
  "confirm_upload_id": "0198f5f4-95ae-7c74-aea4-6f6e1c12e4bb",
  "expected_state_version": 8,
  "reason": "staging objects remain after terminal upload"
}
```

该命令只读取上传任务中固化的 `staging_backend/staging_prefix`，不接受调用方提供对象 key、前缀或任意 payload。只有 `Completed/Cancelled/Expired/Failed` 终态可执行：不存在 `staging-cleanup:{upload_id}` 时计划 `create`；同一任务已 `Succeeded` 时计划 `rearm_succeeded`；`Pending/Running/Retry` 说明已有恢复责任方，`DeadLetter` 必须使用精确任务 ID 重放，两者均 `eligible=false`。

实际执行要求 `dry_run=false`、匹配路径的 `confirm_upload_id`、精确 `expected_state_version` 和 1-256 字符原因。任务创建/重置与 `admin.upload.cleanup_rebuild` 审计同事务提交；响应包含 `planned_action/rebuilt` 及可选的 `job_id/job_status`。该接口不直接删除对象。

#### 发起存储对账

**POST** `/api/admin/storage-reconciliation/{scan_id}/enqueue`

```json
{
  "scope": "staging",
  "dry_run": true,
  "confirm_scan_id": "ops-20260720-staging-01",
  "reason": "verify staging after storage incident"
}
```

`scan_id` 限 1-64 个 `[A-Za-z0-9._:-]` 字符；`scope` 必须是 `contents/users/staging/final` 之一。命令只创建该 scope 的首个有界分页 `storage_reconcile` 任务：数据库 scope 页大小固定 500，对象 scope 固定 1000，后续页仍由 Worker 的既有任务合同产生。

dry-run 返回派生的稳定去重键和是否可入队，不写库。实际执行要求 `dry_run=false`、完全匹配的 `confirm_scan_id` 和 1-256 字符原因；相同 `scan_id + scope` 的首任务已存在时返回 HTTP 409，不重置历史任务。入队与 `admin.storage.reconcile` 审计同事务提交。接口不接受游标、页码、SQL 或自定义任务 payload。

以上三个写命令都必须经过 `AdminAuthFilter` 后再进入管理员限流。dry-run、校验失败、状态不合格和并发冲突均不得写 `operation_logs`；只有实际状态/任务变化与审计同时成功才返回成功。

#### 精确查询恢复审计

**GET** `/api/admin/logs`

该管理员日志接口支持 `page`、`page_size`、`action`、`target_type`、`target_name`、`start_date` 和
`end_date`。`page_size` 最大为 100；`action` 限 1-128 个、`target_type` 限 1-64 个
`[A-Za-z0-9._:-]` 字符；`target_name` 最长 255 字节且不得包含 ASCII 控制字符；日期必须是有效的
`YYYY-MM-DD`，并满足 `start_date <= end_date`。三个目标筛选均为精确匹配，日期范围包含起止两日。

所有可选条件必须作为 PostgreSQL 参数绑定，不得拼接到 SQL。`data.items[]` 返回
`id/user_id/action/target_type/target_id/target_name/details/ip_address/created_at`；可空的
`user_id/target_id/target_name/details` 使用 JSON `null`。字符串资源（例如 upload ID 和对账 scan ID）
通过 `target_name` 返回，因此运维人员可以仅使用管理 API 精确核对恢复审计，无需读取或修改数据库。

上传租约解除后的核对示例：

```text
GET /api/admin/logs?action=admin.upload.lease_release&target_type=upload&target_name={upload_id}&page_size=20
```

### 10.0.1 内部指标接口

**GET** `/metrics`

返回 Prometheus text exposition，供私网监控抓取，不使用用户 JWT。公网反向代理必须精确拒绝 `/metrics`，监控系统直接访问 Pod/实例端口。指标至少包括：

- `disk_http_requests_total{operation,status_class}` 与 `disk_http_request_duration_seconds{operation}`；错误率由 `4xx/5xx` counter 的区间增量除以请求总增量计算；
- `disk_upload_tasks_active`，以及按固定状态聚合的上传任务数、存储任务数、最老可执行任务年龄、过期租约和 dead-letter 数；活动上传定义为 PostgreSQL 中 `InProgress + Finalizing`；
- `disk_upload_chunks_total`、`disk_upload_chunk_bytes_total`；只累计 staging 写入成功且 PostgreSQL 接受的分片请求，吞吐由 counter 的 `rate` 计算；
- `disk_upload_complete_stage_duration_seconds{stage}`；`stage` 固定为 `claim_lease/load_metadata/assemble/dedup_lookup/promote/commit`，已开始的失败阶段也计入，未执行的提升阶段不产生样本；
- 按固定 `finding_type` 枚举聚合的未解决一致性 finding 数；未知类型归入 `unknown`，不得把资源 ID 或对象 key 用作标签；
- 当前进程角色、启动状态、drain 状态、API 在途请求；
- 指标快照数据库查询是否成功。

标签集合由代码枚举，禁止使用请求路径参数、`request_id`、`upload_id`、文件名、对象 key、`job_id`、租约 owner 或异常文本。数据库快照失败时仍返回进程内指标，并将 `disk_metrics_snapshot_success` 置为 0。数据库快照 gauge 在每个实例上重复暴露，跨副本聚合必须取 `max`；进程内 counter/histogram 才按实例求和。

### 10.1 管理员接口说明

本章节所有接口均需要管理员权限，调用时必须携带有效的管理员 Access Token。

#### 认证要求

所有管理员接口需要在请求头中携带有效的管理员 Access Token：

```
Authorization: Bearer <access_token>
```

#### 管理员错误响应（统一）

管理员接口统一使用以下错误码，不再每个接口中重复展开：

| 错误码 | 枚举名称 | HTTP状态码 | 说明 |
|--------|----------|------------|------|
| 80001 | `AdminRequired` | 403 | 需要管理员权限 |
| 80002 | `AdminUserNotFound` | 404 | 用户不存在 |
| 80003 | `AdminCannotModifySelf` | 400 | 不能修改自己的状态或角色 |
| 80004 | `AdminCannotDemoteLast` | 400 | 不能降级最后一个管理员 |
| 80005 | `AdminShareNotFound` | 404 | 分享不存在 |
| 80006 | `AdminInvalidStatus` | 400 | 无效的用户状态 |
| 80007 | `AdminInvalidRole` | 400 | 无效的角色 |

**80001 AdminRequired 响应示例**：

```json
{
  "code": 80001,
  "message": "需要管理员权限",
  "data": null
}
```

**80002 AdminUserNotFound 响应示例**：

```json
{
  "code": 80002,
  "message": "用户不存在",
  "data": {
    "user_id": 99999,
    "reason": "指定的用户不存在"
  }
}
```

**80003 AdminCannotModifySelf 响应示例**：

```json
{
  "code": 80003,
  "message": "不能修改自己的状态或角色",
  "data": null
}
```

**80004 AdminCannotDemoteLast 响应示例**：

```json
{
  "code": 80004,
  "message": "不能降级最后一个管理员",
  "data": null
}
```

**80005 AdminShareNotFound 响应示例**：

```json
{
  "code": 80005,
  "message": "分享不存在",
  "data": {
    "share_id": "sh_invalid",
    "reason": "指定的分享不存在"
  }
}
```

**80006 AdminInvalidStatus 响应示例**：

```json
{
  "code": 80006,
  "message": "无效的用户状态",
  "data": {
    "invalid_value": 5,
    "reason": "用户状态必须是 0（禁用）、1（正常）或 2（锁定）"
  }
}
```

**80007 AdminInvalidRole 响应示例**：

```json
{
  "code": 80007,
  "message": "无效的角色",
  "data": {
    "invalid_value": 5,
    "reason": "用户角色必须是 0（普通用户）或 1（管理员）"
  }
}
```

#### 通用认证错误

除管理员专用错误外，还可能返回以下通用认证错误：

| 错误码 | 枚举名称 | HTTP状态码 | 说明 |
|--------|----------|------------|------|
| 40106 | `TokenMissing` | 401 | 未提供令牌 |
| 40107 | `TokenMalformed` | 401 | 令牌格式错误 |
| 40108 | `TokenExpired` | 401 | 令牌已过期 |
| 40109 | `TokenWrongType` | 401 | 令牌类型错误（需要 access token） |
| 10001 | `InvalidParameter` | 400 | 请求参数错误 |
| 10002 | `ValidationFailed` | 400 | 参数校验失败 |

---

### 10.2 获取用户列表

**GET** `/api/admin/users`

获取所有用户列表，支持分页和筛选。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌（需要管理员权限） |

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
 | username | string | 否 | 用户名筛选（模糊匹配） |
 | email | string | 否 | 邮箱筛选（模糊匹配） |
 | status | integer | 否 | 状态筛选：0（禁用）/1（正常）/2（锁定） |
 | role | integer | 否 | 角色筛选：0（普通用户）/1（管理员） |
 | page | integer | 否 | 页码，默认 1 |
 | page_size | integer | 否 | 每页数量，默认 20，最大 100 |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 字段值不符合规则 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 403 | 80001 | `AdminRequired` | 需要管理员权限 | 非管理员用户访问 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "items": [
      {
        "id": 1,
        "username": "john_doe",
        "email": "john@example.com",
        "nickname": "John",
        "status": 1,
        "role": 0,
        "storage_used": 1073741824,
        "storage_quota": 10737418240,
        "file_count": 150,
        "folder_count": 20,
        "created_at": "2026-01-01T00:00:00Z",
        "updated_at": "2026-01-10T12:00:00Z"
      },
      {
        "id": 2,
        "username": "admin_user",
        "email": "admin@example.com",
        "nickname": "Admin",
        "status": 1,
        "role": 1,
        "storage_used": 5368709120,
        "storage_quota": 53687091200,
        "file_count": 500,
        "folder_count": 50,
        "created_at": "2025-12-01T00:00:00Z",
        "updated_at": "2026-01-15T08:30:00Z"
      }
    ],
    "pagination": {
      "page": 1,
      "page_size": 20,
      "total": 100,
      "total_pages": 5
    }
  }
}
```

---

### 10.3 获取用户详情

**GET** `/api/admin/users/{id}`

获取指定用户的详细信息。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌（需要管理员权限） |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| id | integer | 用户 ID |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | user_id 格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 403 | 80001 | `AdminRequired` | 需要管理员权限 | 非管理员用户访问 |
| 404 | 80002 | `AdminUserNotFound` | 用户不存在 | 指定的用户 ID 不存在 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "id": 1,
    "username": "john_doe",
    "email": "john@example.com",
    "nickname": "John",
    "avatar": "https://example.com/avatar/1.jpg",
    "status": 1,
    "role": 0,
    "storage_used": 1073741824,
    "storage_quota": 10737418240,
    "file_count": 150,
    "folder_count": 20,
    "created_at": "2026-01-01T00:00:00Z",
    "updated_at": "2026-01-10T12:00:00Z"
  }
}
```

---

### 10.4 修改用户状态

**PUT** `/api/admin/users/{id}/status`

修改指定用户的状态（禁用/正常/锁定）。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌（需要管理员权限） |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| id | integer | 用户 ID |

#### 请求参数

```json
{
  "status": 0
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| status | integer | 是 | 用户状态：0（禁用）/1（正常）/2（锁定） |

#### 业务规则

1. **禁止修改自己**：管理员不能修改自己的状态
2. **状态值范围**：仅允许 0、1、2 三个值
3. **最后管理员保护**：不能将最后一个管理员的状态改为非正常状态
4. **锁定状态边界**：管理员修改任一状态时清零 `login_attempts/locked_until`；`status=2` 因而表示无自动截止时间的管理员锁定，只有后续管理员状态变更可以解除

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 字段值不符合规则 |
| 400 | 80006 | `AdminInvalidStatus` | 无效的用户状态 | status 不是 0、1、2 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 403 | 80001 | `AdminRequired` | 需要管理员权限 | 非管理员用户访问 |
| 403 | 80003 | `AdminCannotModifySelf` | 不能修改自己的状态 | 尝试修改自己的状态 |
| 403 | 80004 | `AdminCannotDemoteLast` | 不能降级最后一个管理员 | 尝试禁用最后一个管理员 |
| 404 | 80002 | `AdminUserNotFound` | 用户不存在 | 指定的用户 ID 不存在 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "id": 1,
    "status": 0,
    "updated_at": "2026-01-15T10:00:00Z"
  }
}
```

---

### 10.5 修改用户角色

**PUT** `/api/admin/users/{id}/role`

修改指定用户的角色（普通用户/管理员）。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌（需要管理员权限） |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| id | integer | 用户 ID |

#### 请求参数

```json
{
  "role": 0
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| role | integer | 是 | 用户角色：0（普通用户）/1（管理员） |

#### 业务规则

1. **禁止修改自己**：管理员不能修改自己的角色
2. **角色值范围**：仅允许 0、1 两个值
3. **最后管理员保护**：不能将最后一个管理员降级为普通用户

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 字段值不符合规则 |
| 400 | 80007 | `AdminInvalidRole` | 无效的角色 | role 不是 0 或 1 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 403 | 80001 | `AdminRequired` | 需要管理员权限 | 非管理员用户访问 |
| 403 | 80003 | `AdminCannotModifySelf` | 不能修改自己的角色 | 尝试修改自己的角色 |
| 403 | 80004 | `AdminCannotDemoteLast` | 不能降级最后一个管理员 | 尝试降级最后一个管理员 |
| 404 | 80002 | `AdminUserNotFound` | 用户不存在 | 指定的用户 ID 不存在 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "id": 1,
    "role": 0,
    "updated_at": "2026-01-15T10:00:00Z"
  }
}
```

---

### 10.6 删除用户

**DELETE** `/api/admin/users/{id}`

删除指定用户（软删除，将用户状态置为禁用）。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌（需要管理员权限） |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| id | integer | 用户 ID |

#### 业务规则

1. **软删除**：执行软删除操作，将用户 status 置为 0（禁用）
2. **禁止删除自己**：管理员不能删除自己
3. **最后管理员保护**：不能删除最后一个管理员
4. **用户数据保留**：用户文件数据保留，仅禁用账户访问

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | user_id 格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 403 | 80001 | `AdminRequired` | 需要管理员权限 | 非管理员用户访问 |
| 403 | 80003 | `AdminCannotModifySelf` | 不能修改自己的状态 | 尝试删除自己 |
| 403 | 80004 | `AdminCannotDemoteLast` | 不能降级最后一个管理员 | 尝试删除最后一个管理员 |
| 404 | 80002 | `AdminUserNotFound` | 用户不存在 | 指定的用户 ID 不存在 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "id": 1,
    "status": 0,
    "updated_at": "2026-01-15T10:00:00Z"
  }
}
```

---

### 10.7 获取全局存储统计

**GET** `/api/admin/storage/stats`

获取系统全局的存储空间统计信息。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌（需要管理员权限） |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 403 | 80001 | `AdminRequired` | 需要管理员权限 | 非管理员用户访问 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "total_users": 100,
    "total_files": 5000,
    "total_folders": 200,
    "total_size": 549755813888,
    "user_count": 100,
    "active_user_count": 95
  }
}
```

---

### 10.8 获取分享列表

**GET** `/api/admin/shares`

获取所有分享列表，支持分页和筛选。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌（需要管理员权限） |

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
 | status | integer | 否 | 状态筛选：0（已取消）/1（有效）/2（已过期） |
 | user_id | integer | 否 | 按创建用户筛选 |
 | username | string | 否 | 按分享者用户名模糊筛选 |
| page | integer | 否 | 页码，默认 1 |
| page_size | integer | 否 | 每页数量，默认 20，最大 100 |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | 参数格式错误 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 字段值不符合规则 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 403 | 80001 | `AdminRequired` | 需要管理员权限 | 非管理员用户访问 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "items": [
      {
        "share_id": "sh_abc123",
        "user_id": 1,
        "username": "john_doe",
        "file_name": "文档.pdf",
        "file_count": 1,
        "share_link": "https://disk.example.com/s/abc123",
        "has_password": true,
        "permission": "download",
        "view_count": 10,
        "download_count": 5,
        "created_at": "2026-01-13T10:00:00Z",
        "expires_at": "2026-01-20T10:00:00Z",
        "status": 1
      }
    ],
    "pagination": {
      "page": 1,
      "page_size": 20,
      "total": 50,
      "total_pages": 3
    }
  }
}
```

---

### 10.9 获取分享详情

**GET** `/api/admin/shares/{id}`

获取指定分享的详细信息（包括关联文件）。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌（需要管理员权限） |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| id | string | 分享 ID（share_code） |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | share_id 格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 403 | 80001 | `AdminRequired` | 需要管理员权限 | 非管理员用户访问 |
| 404 | 80005 | `AdminShareNotFound` | 分享不存在 | 指定的分享不存在 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "share_id": "sh_abc123",
    "user_id": 1,
    "username": "john_doe",
    "files": [
      {"id": 1, "name": "文档.pdf", "type": "file", "size": 102400}
    ],
    "share_link": "https://disk.example.com/s/abc123",
    "has_password": true,
    "permission": "download",
    "view_count": 10,
    "download_count": 5,
    "created_at": "2026-01-13T10:00:00Z",
    "expires_at": "2026-01-20T10:00:00Z",
    "status": "active"
  }
}
```

---

### 10.10 删除分享

**DELETE** `/api/admin/shares/{id}`

强制取消指定分享（管理员操作）。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌（需要管理员权限） |

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| id | string | 分享 ID（share_code） |

#### 业务规则

管理员可以强制取消任何分享，被取消的分享立即失效。

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | share_id 格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 403 | 80001 | `AdminRequired` | 需要管理员权限 | 非管理员用户访问 |
| 404 | 80005 | `AdminShareNotFound` | 分享不存在 | 指定的分享不存在 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "share_id": "sh_abc123",
    "status": "cancelled",
    "updated_at": "2026-01-15T10:00:00Z"
  }
}
```

---

### 10.11 获取系统概览统计

**GET** `/api/admin/stats/overview`

获取系统整体运行状态的统计信息。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌（需要管理员权限） |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 403 | 80001 | `AdminRequired` | 需要管理员权限 | 非管理员用户访问 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "user_count": 100,
    "file_count": 5000,
    "storage_size": 549755813888,
    "share_count": 200,
    "active_share_count": 150,
    "storage_quota": 10995116277760
  }
}
```

---

### 10.12 获取系统状态

**GET** `/api/admin/stats/system`

获取系统运行状态详细信息（数据库连接、Redis 连接、磁盘空间、运行时间）。

#### 请求头

```
Authorization: Bearer <access_token>
```

| Header | 必填 | 说明 |
|--------|------|------|
| Authorization | 是 | Bearer 访问令牌（需要管理员权限） |

#### 响应字段

| 字段 | 类型 | 说明 |
|------|------|------|
| uptime | integer | 运行时间（秒） |
| version | string | 系统版本 |
| database | object | 数据库连接状态 |
| database.connected | boolean | 是否已连接 |
| database.connection_count | integer | 当前连接数 |
| database.latency_ms | integer | 查询延迟（毫秒） |
| redis | object | Redis 连接状态 |
| redis.connected | boolean | 是否已连接 |
| redis.latency_ms | integer | 查询延迟（毫秒） |
| disk | object | 磁盘空间状态 |
| disk.total | integer | 总空间（字节） |
| disk.used | integer | 已用空间（字节） |
| disk.free | integer | 可用空间（字节） |
| disk.percentage | double | 使用百分比 |

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 403 | 80001 | `AdminRequired` | 需要管理员权限 | 非管理员用户访问 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "uptime": 86400,
    "version": "1.0.0",
    "database": {
      "connected": true,
      "connection_count": 5,
      "latency_ms": 3
    },
    "redis": {
      "connected": true,
      "latency_ms": 1
    },
    "disk": {
      "total": 10995116277760,
      "used": 5497558138880,
      "free": 5497558138880,
      "percentage": 50.0
    }
  }
}
```
