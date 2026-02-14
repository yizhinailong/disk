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
| Content-Type | 是 | `application/json`（文件上传时为 `multipart/form-data`） |
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
| 40003 | `InvalidFormat` | 400 | 参数格式不正确 |
| 40101 | `InvalidCredentials` | 401 | 用户名或密码错误 |
| 40102 | `AccountLocked` | 401 | 账户已锁定 |
| 40103 | `AccountDisabled` | 401 | 账户已禁用 |
| 40104 | `InvalidToken` | 401 | 令牌无效或已过期 |
| 40105 | `InvalidRefreshToken` | 401 | 刷新令牌无效 |
| 40106 | `TokenMissing` | 401 | 未提供令牌 |
| 40107 | `TokenMalformed` | 401 | 令牌格式错误 |
| 40108 | `TokenExpired` | 401 | 令牌已过期 |
| 40109 | `TokenWrongType` | 401 | 令牌类型错误 |

#### 文件错误码

| 错误码 | 枚举名称 | HTTP状态码 | 说明 |
|--------|----------|------------|------|
| 50001 | `InvalidFilename` | 400 | 文件名无效 |
| 50002 | `FileTypeNotAllowed` | 400 | 文件类型不允许 |
| 50003 | `FileSizeExceeded` | 400 | 文件大小超出限制 |
| 50004 | `StorageQuotaExceeded` | 400 | 存储空间不足 |
| 50005 | `FileNotFound` | 404 | 文件不存在 |
| 50006 | `FolderNotFound` | 404 | 文件夹不存在 |
| 50007 | `FileAlreadyExists` | 409 | 同名文件已存在 |
| 50008 | `UploadTaskNotFound` | 400 | 上传任务不存在或已过期 |
| 50009 | `ChunkVerifyFailed` | 400 | 分片校验失败 |
| 50010 | `FolderAlreadyExists` | 409 | 同名文件夹已存在 |

#### 分享错误码

| 错误码 | 枚举名称 | HTTP状态码 | 说明 |
|--------|----------|------------|------|
| 60001 | `ShareNotFound` | 404 | 分享不存在 |
| 60002 | `ShareExpired` | 400 | 分享已过期 |
| 60003 | `SharePasswordError` | 400 | 分享密码错误 |
| 60004 | `ShareAccessDenied` | 403 | 无权限访问 |

#### 代码使用示例

```cpp
#include "utils/ErroCode.hpp"

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
| password | string | 是 | 密码，8-64字符，需含大小写字母和数字 |

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
**未实现**

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
| nickname | string | 否 | 昵称，2-32字符 |
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
**未实现**

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
**⏸️ 待实现**（依赖文件功能完成）

> **前置条件**：需要先完成 FileController/FileService 和 FolderController/FolderService 的实现，包括文件上传、删除、文件夹管理和回收站功能。

获取用户存储空间使用详情。

#### 实现说明（计划中）

- `storage_used` 将使用实时 SQL 计算，不读取 `users.storage_used` 字段
- 回收站（Trash）中的文件不计入存储统计
- `categories` 当前版本将返回空数组，后续版本支持文件类型分类
- 详细实现计划见：`.sisyphus/plans/storage-api.md`

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
    "used": 1073741824,
    "quota": 10737418240,
    "percentage": 10.0,
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

初始化文件上传任务，检测秒传和断点续传。

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

#### 响应示例 - 正常上传

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "upload_id": "up_abc123def456",
    "chunk_size": 5242880,
    "total_chunks": 20,
    "uploaded_chunks": [],
    "instant_upload": false
  }
}
```

#### 响应示例 - 秒传成功

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "instant_upload": true,
    "file": {
      "id": 123,
      "name": "document.pdf",
      "size": 104857600,
      "created_at": "2026-01-13T10:30:00Z"
    }
  }
}
```

#### 响应示例 - 断点续传

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "upload_id": "up_abc123def456",
    "chunk_size": 5242880,
    "total_chunks": 20,
    "uploaded_chunks": [0, 1, 2, 3, 4],
    "instant_upload": false
  }
}
```

---

### 4.2 上传分片

**POST** `/api/file/upload/chunk`

上传文件分片。

#### 请求格式

`Content-Type: multipart/form-data`

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| upload_id | string | 是 | 上传任务 ID |
| chunk_index | integer | 是 | 分片索引（从 0 开始） |
| chunk_hash | string | 是 | 分片 MD5 哈希 |
| chunk | file | 是 | 分片数据 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "chunk_index": 5,
    "uploaded": true
  }
}
```

---

### 4.3 完成上传

**POST** `/api/file/upload/complete`

完成文件上传，合并分片。

#### 请求参数

```json
{
  "upload_id": "up_abc123def456"
}
```

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
      "created_at": "2026-01-13T10:35:00Z"
    }
  }
}
```

---

### 4.4 取消上传

**DELETE** `/api/file/upload/{upload_id}`

取消上传任务，清理临时分片。

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| upload_id | string | 上传任务 ID |

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

获取文件下载信息。

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| file_id | integer | 文件 ID |

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

下载文件内容，支持 Range 请求。

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| file_id | integer | 文件 ID |

#### 请求头

| Header | 说明 |
|--------|------|
| Range | 可选，字节范围，如 `bytes=0-1048575` |

#### 响应头

| Header | 说明 |
|--------|------|
| Content-Type | 文件 MIME 类型 |
| Content-Length | 返回数据长度 |
| Content-Disposition | `attachment; filename="document.pdf"` |
| Accept-Ranges | `bytes` |
| Content-Range | 范围信息（Range 请求时） |

#### 响应

文件二进制数据流。

---

### 4.7 获取文件列表

**GET** `/api/file/list`

获取指定目录下的文件和文件夹列表。

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| parent_id | integer | 否 | 父文件夹 ID，默认 0 |
| page | integer | 否 | 页码，默认 1 |
| page_size | integer | 否 | 每页数量，默认 20，最大 100 |
| sort_by | string | 否 | 排序字段：name/size/created_at/updated_at |
| sort_order | string | 否 | 排序方向：asc/desc，默认 asc |
| type | string | 否 | 筛选类型：all/file/folder，默认 all |

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

### 4.8 获取文件详情

**GET** `/api/file/{file_id}`

获取单个文件的详细信息。

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| file_id | integer | 文件 ID |

#### 响应示例

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

### 4.9 重命名文件

**PUT** `/api/file/{file_id}/rename`

重命名文件或文件夹。

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

### 4.10 移动文件

**PUT** `/api/file/move`

移动文件或文件夹到指定目录。

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

### 4.11 复制文件

**POST** `/api/file/copy`

复制文件或文件夹到指定目录。

#### 请求参数

```json
{
  "file_ids": [1, 2, 3],
  "target_folder_id": 10
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

### 4.12 删除文件

**DELETE** `/api/file`

删除文件或文件夹（移入回收站）。

#### 请求参数

```json
{
  "file_ids": [1, 2, 3]
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

### 4.13 搜索文件

**GET** `/api/file/search`

搜索用户文件。

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| keyword | string | 是 | 搜索关键词 |
| type | string | 否 | 文件类型：document/image/video/audio |
| folder_id | integer | 否 | 限定搜索范围 |
| page | integer | 否 | 页码 |
| page_size | integer | 否 | 每页数量 |

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "items": [
      {
        "id": 123,
        "name": "报告2026.pdf",
        "type": "file",
        "size": 102400,
        "path": "/工作/报告2026.pdf",
        "created_at": "2026-01-10T10:00:00Z"
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

## 5. 文件夹接口

### 5.1 创建文件夹

**POST** `/api/folder/create`

#### 实现状态
**未实现**

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

获取文件夹目录树结构。

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| parent_id | integer | 否 | 起始文件夹 ID，默认 0 |
| depth | integer | 否 | 展开深度，默认 -1（全部） |

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

#### 路径参数

| 参数 | 类型 | 说明 |
|------|------|------|
| folder_id | integer | 文件夹 ID |

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

获取回收站中的文件列表。

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| page | integer | 否 | 页码，默认 1 |
| page_size | integer | 否 | 每页数量，默认 20 |

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

---

### 6.2 恢复文件

**POST** `/api/trash/restore`

从回收站恢复文件。

#### 请求参数

```json
{
  "trash_ids": [1, 2, 3]
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "restored_count": 3,
    "restored_files": [
      {"trash_id": 1, "file_id": 123, "path": "/文档/已删除文件.pdf"},
      {"trash_id": 2, "file_id": 124, "path": "/已删除文件2.pdf"},
      {"trash_id": 3, "file_id": 125, "path": "/已删除文件3.pdf"}
    ]
  }
}
```

---

### 6.3 彻底删除

**DELETE** `/api/trash`

彻底删除回收站中的文件。

#### 请求参数

```json
{
  "trash_ids": [1, 2, 3]
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "deleted_count": 3,
    "freed_space": 307200
  }
}
```

---

### 6.4 清空回收站

**DELETE** `/api/trash/all`

清空回收站所有内容。

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

### 7.1 创建分享

**POST** `/api/share`

创建文件分享链接。

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

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| status | string | 否 | 状态筛选：all/active/expired |
| page | integer | 否 | 页码 |
| page_size | integer | 否 | 每页数量 |

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

#### 请求参数

```json
{
  "share_ids": ["sh_abc123", "sh_def456"]
}
```

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "cancelled_count": 2
  }
}
```

---

### 7.6 验证分享访问

**POST** `/api/share/access/{share_id}`

验证分享并获取访问令牌（供访客使用，无需登录）。

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

#### 请求头

```
X-Share-Token: st_xyz789...
```

#### 查询参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| folder_id | integer | 否 | 文件夹 ID（分享内的相对 ID） |

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

#### 请求头

```
X-Share-Token: st_xyz789...
Range: bytes=0-1048575 (可选)
```

#### 响应

文件二进制数据流。

---

## 8. 系统接口

### 8.1 健康检查

**GET** `/api/health`

系统健康检查（无需认证）。

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "status": "healthy",
    "version": "1.0.0",
    "uptime": 86400,
    "timestamp": "2026-01-13T10:00:00Z"
  }
}
```

---

### 8.2 系统信息

**GET** `/api/system/info`

获取系统信息（需要管理员权限）。

#### 响应示例

```json
{
  "code": 0,
  "message": "success",
  "data": {
    "version": "1.0.0",
    "drogon_version": "1.9.11",
    "build_time": "2026-01-01T00:00:00Z",
    "uptime": 86400,
    "connections": {
      "current": 150,
      "peak": 500,
      "total": 10000
    },
    "storage": {
      "total": 1099511627776,
      "used": 549755813888,
      "available": 549755813888
    }
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
