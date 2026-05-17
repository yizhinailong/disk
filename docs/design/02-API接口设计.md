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
| X-Request-ID | 否 | 请求追踪 ID，用于日志关联 |

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

使当前令牌失效。

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
| filename | string | 是 | 文件名 |
| file_size | integer | 是 | 文件大小（字节） |
| file_hash | string | 是 | 文件 MD5 哈希 |
| parent_id | integer | 否 | 父文件夹 ID，默认 0（根目录） |

> **📤 上传预占用语义（CRITICAL）**：本接口执行**存储空间预占用**操作：
> - `storage_reserved` 增加 `file_size`，防止并发上传超过配额
> - 如果 `storage_used + storage_reserved + file_size > storage_quota`，返回 `400 + 50004 StorageQuotaExceeded`
> - 秒传（文件哈希已存在）不经过预占用流程，直接创建 `files` 记录
> - 取消上传或超时后，预占用的空间通过 `storage_reserved` 释放

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
**已实现**

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
**已实现**

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

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | upload_id 为空或格式错误 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Access Token 已超过有效期 |
| 400 | 50008 | `UploadTaskNotFound` | 上传任务不存在 | upload_id 不存在或已过期 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 分片不完整、哈希校验失败 |

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
> - 清理临时文件和上传任务记录

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
| name | string | 是 | 文件夹名称，1-255字符 |
| parent_id | integer | 否 | 父文件夹 ID，默认 0（根目录） |

#### 文件夹名称约束

| 约束项 | 规则 | 违反后果 |
|--------|------|----------|
| **长度** | 1-255 字符 | 返回 `400 + 10002` 校验失败 |
| **禁止字符** | 禁止以下字符：<br>- `/ \ : * ? " < > |`（文件系统保留字符）<br>- ASCII 控制字符（0x00-0x1F） | 返回 `400 + 50001` 文件名无效 |
| **保留名称** | 禁止 `.` 和 `..`（相对路径标识） | 返回 `400 + 50001` 文件名无效 |
| **隐藏文件夹** | 禁止以 `.` 开头（Unix 隐藏文件约定） | 返回 `400 + 50001` 文件名无效 |
| **字符集** | 仅允许 ASCII 字符（禁止 emoji 和特殊 Unicode） | 返回 `400 + 50001` 文件名无效 |
| **首尾空格** | 自动去除首尾空格后验证 | - |

**有效文件夹名称示例**：
- ✅ `Documents`
- ✅ `工作文件2024`
- ✅ `Project_Alpha`
- ✅ `用户数据.backup`

**无效文件夹名称示例**：
- ❌ `My/Folder`（包含 `/`）
- ❌ `Folder:Name`（包含 `:`）
- ❌ `.`（保留名称）
- ❌ `..`（保留名称）
- ❌ `.hidden`（以 `.` 开头）
- ❌ `📁文件夹`（包含 emoji）

**同名规则**：
- 文件和文件夹可以同名（允许 `Document.pdf` 和 `Document/` 共存）
- 同一目录下不允许存在同名文件夹（返回 `409 + 50010`）

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
| |------------|--------|----------|----------|----------|
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | 文件夹名称长度无效（空或超过255字符） |
| 400 | 50001 | `InvalidFilename` | 文件名无效 | 包含禁止字符、保留名称、隐藏文件夹、非ASCII字符 |
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
3. **V1 限制**：当前版本不支持递归恢复文件夹子树，文件夹类型项目恢复时仅恢复文件夹本身（不包含内部子项）

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

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | file_ids 为空或格式错误 |
| 400 | 10002 | `ValidationFailed` | 参数校验失败 | password 长度不在 4-8 字符之间 |
| 401 | 40106 | `TokenMissing` | 未提供令牌 | 请求头缺少 Authorization |
| 401 | 40108 | `TokenExpired` | 令牌已过期 | Token 已超过有效期 |
| 404 | 50005 | `FileNotFound` | 文件不存在 | 指定的 file_id 不存在或不属于当前用户 |

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
| `active` | `1` | 有效分享（未过期且未取消） |
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

验证分享并获取访问令牌（供访客使用，无需登录）。

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

#### 错误响应矩阵

| HTTP 状态码 | 业务码 | 枚举名称 | 错误消息 | 触发场景 |
|------------|--------|----------|----------|----------|
| 400 | 10001 | `InvalidParameter` | 请求参数错误 | share_id 格式错误 |
| 404 | 60001 | `ShareNotFound` | 分享不存在 | share_id 不存在 |
| 400 | 60002 | `ShareExpired` | 分享已过期 | 分享已超过有效期 |
| 400 | 60003 | `SharePasswordError` | 分享密码错误 | 密码验证失败 |

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

**60003 SharePasswordError 响应示例**：

```json
{
  "code": 60003,
  "message": "分享密码错误",
  "data": {
    "share_id": "sh_abc123",
    "reason": "访问密码验证失败"
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

浏览分享的文件夹内容（使用分享令牌）。

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

下载分享的文件。

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

**GET** `/api/health`

#### 实现状态
**✅ 已实现**

系统健康检查（无需认证）。检查数据库和 Redis 连接状态。

#### 响应字段

| 字段 | 类型 | 说明 |
|------|------|------|
| overall_status | string | 整体状态：`healthy` / `degraded` / `unhealthy` |
| version | string | 系统版本 |
| uptime | integer | 运行时间（秒） |
| timestamp | string | ISO 8601 时间戳 |
| components | object | 各组件状态 |
| components.database | object | 数据库状态 |
| components.redis | object | Redis 状态 |
| components.*.status | string | 组件状态：`healthy` / `unhealthy` |
| components.*.message | string | 错误信息（可选） |
| components.*.latency_ms | integer | 响应延迟（毫秒） |

#### HTTP 状态码

| 状态码 | 说明 |
|--------|------|
| 200 | 系统健康 |
| 503 | 系统不健康或降级 |

#### 成功响应示例（200）

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "overall_status": "healthy",
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
      }
    }
  }
}
```

#### 不健康响应示例（503）

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "overall_status": "unhealthy",
    "version": "1.0.0",
    "uptime": 86400,
    "timestamp": "2026-02-18T12:30:00Z",
    "components": {
      "database": {
        "status": "unhealthy",
        "message": "Connection refused",
        "latency_ms": 0
      },
      "redis": {
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

对于设置了密码的分享，服务端必须实施以下防护措施：

| 防护措施 | 实现要求 | 触发条件 |
|---------|---------|---------|
| **尝试计数** | Redis 计数器 `share:pwd:{share_id}:{ip}` | 每次密码验证失败 |
| **尝试限制** | 最多 5 次失败 | 单个 IP + 单个分享 |
| **临时锁定** | 15 分钟冷却期 | 达到失败上限后 |
| **计数器过期** | 15 分钟 TTL | 最后一次尝试后 |

**Redis 键设计**：
```
share:pwd:{share_id}:{client_ip}  -> 失败次数 (int, TTL=15min)
share:pwd:lock:{share_id}:{client_ip}  -> 锁定标记 (exists, TTL=15min)
```

**错误响应（锁定状态）**：

```json
{
  "code": 60003,
  "message": "分享密码错误",
  "data": {
    "share_id": "sh_abc123",
    "reason": "密码错误次数过多，请 15 分钟后重试",
    "retry_after": 900
  }
}
```

#### 9.4.2 Share Token 防护

Share Token（通过 `/api/share/access` 获取）需要以下安全措施：

| 防护措施 | 实现要求 | 说明 |
|---------|---------|------|
| **短有效期** | 默认 1 小时（3600 秒） | 限制被盗用后的攻击窗口 |
| **单次使用/绑定** | 建议绑定 Client IP 或 User-Agent | 可选增强措施 |
| **撤销机制** | 分享取消时立即使所有 token 失效 | 通过 Redis 黑名单或 DB 状态检查 |
| **类型标识** | JWT `type` claim = `share` | 区分于 access_token/refresh_token |
| **作用域限定** | JWT `scope` claim 包含 `share_id` 和 `permission` | 限定 token 仅能访问指定分享 |

**Share Token Payload 示例**：

```json
{
  "sub": "sh_abc123",
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

**Token 撤销检查**：

服务端在验证 Share Token 时，必须检查：
1. JWT 签名和过期时间
2. 分享状态（`shares.status`）：`status = 0` (cancelled) 时拒绝
3. 分享有效期（`shares.expires_at`）：已过期时拒绝
4. （可选）Redis 黑名单 `share:revoked:{jti}`

**已撤销 Token 响应**：

```json
{
  "code": 40104,
  "message": "令牌无效或已过期",
  "data": {
    "reason": "分享已被取消",
    "share_id": "sh_abc123"
  }
}
```

#### 9.4.3 速率限制强化

除通用分享访问限制外，对敏感操作实施更严格的限制：

| 操作类型 | 限制规则 | 说明 |
|---------|---------|------|
| **分享访问验证** | 30 次/分钟/IP | `/api/share/access` 接口 |
| **密码尝试** | 5 次/15分钟/IP/分享 | 同一分享的密码验证 |
| **文件下载** | 10 次/分钟/share_token | 限制下载频率 |
| **浏览目录** | 60 次/分钟/share_token | 限制目录遍历 |

**429 Too Many Requests 响应**：

```json
{
  "code": 10005,
  "message": "请求过于频繁",
  "data": {
    "retry_after": 60,
    "limit": "30 次/分钟"
  }
}
```

#### 9.4.4 审计日志

以下分享相关事件必须记录到 `operation_logs` 表：

| 事件类型 | `action` 值 | 记录字段 |
|---------|------------|---------|
| **分享创建** | `share_create` | `target_type`='share', `target_id`=shares.id, `details`={share_code, file_ids, permission, expires_at} |
| **分享访问** | `share_access` | `target_type`='share', `target_id`=shares.id, `details`={share_code, ip, user_agent, success} |
| **密码验证失败** | `share_pwd_fail` | `target_type`='share', `target_id`=shares.id, `details`={share_code, ip, attempt_count} |
| **文件下载** | `share_download` | `target_type`='share', `target_id`=shares.id, `details`={share_code, file_id, ip, bytes} |
| **分享取消** | `share_cancel` | `target_type`='share', `target_id`=shares.id, `details`={share_code, cancelled_by} |

> **注意**：`target_id` 为数据库内部 `shares.id`（BIGINT），外部标识 `share_code` 存储于 `details` JSON 中用于追溯。

**审计日志字段要求**（对应 `sql/init.sql` 定义）：

```sql
-- operation_logs 表关键字段
user_id     BIGINT UNSIGNED  -- 操作者 ID（访客为 NULL，需调整 NOT NULL 约束）
action      VARCHAR(32)      -- 操作类型
target_type VARCHAR(32)      -- 目标类型（如 'share', 'file', 'folder'）
target_id   BIGINT UNSIGNED  -- 目标内部 ID（shares.id，非 share_code）
target_name VARCHAR(255)     -- 目标名称（可选，如分享的文件名）
details     JSON             -- 操作详情（含 share_code 等外部标识）
ip_address  VARCHAR(45)      -- 客户端 IP
user_agent  VARCHAR(512)     -- 客户端标识
created_at  DATETIME         -- 操作时间
```

#### 9.4.5 安全检查清单

实现分享功能时，必须确保以下检查项全部通过：

- [ ] 密码验证失败计数器使用 Redis 原子操作（INCR + EXPIRE）
- [ ] Share Token 有效期不超过 1 小时
- [ ] Share Token 包含 `type: "share"` 和 `scope` claim
- [ ] 验证 Share Token 时检查分享状态和有效期
- [ ] 分享取消时清理或使失效相关 Share Token
- [ ] 敏感操作有独立的速率限制
- [ ] 关键事件写入审计日志
- [ ] 密码错误响应不泄露分享是否存在的信息（统一返回 60003）

---

## 10. 管理员接口

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

### 10.7 获取用户文件列表

**GET** `/api/admin/users/{id}/files`

获取指定用户的文件列表（管理员查看用户数据）。

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

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| folder_id | integer | 否 | 父文件夹 ID，默认 0（根目录） |
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
| 404 | 80002 | `AdminUserNotFound` | 用户不存在 | 指定的用户 ID 不存在 |

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

### 10.8 获取用户存储统计

**GET** `/api/admin/users/{id}/storage`

获取指定用户的存储空间统计信息。

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
    "used": 1073741824,
    "quota": 10737418240,
    "percentage": 10.0,
    "file_count": 150,
    "folder_count": 20
  }
}
```

---

### 10.9 获取全局存储统计

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

### 10.10 获取分享列表

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
| status | string | 否 | 状态筛选：all/active/expired/cancelled，默认 all |
| user_id | integer | 否 | 按创建用户筛选 |
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
        "status": "active"
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

### 10.11 获取分享详情

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

### 10.12 删除分享

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

### 10.13 获取系统概览统计

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

### 10.14 获取系统状态

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
| mysql | object | MySQL 连接状态 |
| mysql.connected | boolean | 是否已连接 |
| mysql.connection_count | integer | 当前连接数 |
| mysql.latency_ms | integer | 查询延迟（毫秒） |
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
    "mysql": {
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
