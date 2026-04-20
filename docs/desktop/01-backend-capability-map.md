# 桌面客户端 — 后端能力映射

## 1. 文档说明

### 1.1 文档目的

本文档以桌面端用户流程为主线，将后端已有 API 按交互场景分类整理，为后续桌面客户端的领域模型、网络层和导航架构设计提供唯一的路由级参考基线。

### 1.2 读者对象

- 桌面端 C++/QML 开发人员
- 后端 API 对接人员
- 测试人员

### 1.3 真实来源

| 来源 | 路径 | 说明 |
|------|------|------|
| 路由级权威 | `src/controllers/*Controller.hpp` 的 `METHOD_LIST_BEGIN` 块 | 每个路由的 HTTP 方法、路径、过滤器链以此为准 |
| 请求/响应语义 | `docs/design/02-API接口设计.md` | 参数校验、错误码、分页规则、批量语义的权威说明 |
| 认证过滤器行为 | `src/filters/JwtAuthFilter.cpp`、`src/filters/ShareAuthFilter.cpp` | JWT 校验流程、Share Token 校验流程 |
| 全局过滤器与豁免路径 | `config.json` → `plugins` → `GlobalFilters` | 全局 JWT + 限流过滤器的豁免正则列表 |
| 范围与术语 | `docs/desktop/00-scope-and-success.md` | 功能编号（O-01 ~ O-29、V-01 ~ V-04）、术语定义 |

> **路由冲突时的优先级**：控制器头文件 > API 设计文档。若两者不一致，以控制器注册的实际路由为准。

### 1.4 认证域速览

| 认证域 | 令牌类型 | 请求头 | 有效期 | 获取方式 |
|--------|----------|--------|--------|----------|
| 所有者（Owner） | Access Token | `Authorization: Bearer <token>` | 2 小时 | `POST /api/auth/login` |
| 所有者（Owner） | Refresh Token | 请求体 `refresh_token` 字段 | 7 天 | `POST /api/auth/login` |
| 访客（Visitor） | Share Token | `X-Share-Token: <token>` | 1 小时 | `POST /api/share/access/{share_id}` |

两类令牌 **不可混用**。后端通过 `JwtAuthFilter` 和 `ShareAuthFilter` 分别校验，校验失败返回同类错误码族（401xx）。

---

## 2. 认证与会话能力映射

| 能力 | 路由 | 认证模式 | 返回形态 | 桌面端注意事项 |
|------|------|----------|----------|----------------|
| 用户注册 | `POST /api/auth/register` | 公开 | `{ user: { id, username, email, nickname, storage_quota, storage_used, created_at } }` | 注册成功不返回令牌；需引导用户登录 |
| 用户登录 | `POST /api/auth/login` | 公开 | `{ access_token, refresh_token, token_type, expires_in, user }` | 安全存储两个令牌；`user` 包含基础信息可用于初始化 UI |
| 刷新令牌 | `POST /api/auth/refresh` | 公开 | `{ access_token, refresh_token, expires_in }` | Refresh Token **单次使用**，每次刷新后旧 token 立即失效；桌面端必须用新 refresh_token 替换旧的 |
| 用户登出 | `POST /api/auth/logout` | JWT Bearer | `{}`（data 为 null） | 需先携带有效 Access Token 调用；调用后清除本地令牌存储 |

**错误码族**：

| 错误码 | 枚举 | 说明 |
|--------|------|------|
| 40001 | `UsernameExists` | 用户名已存在 |
| 40002 | `EmailExists` | 邮箱已存在 |
| 40101 | `InvalidCredentials` | 用户名或密码错误 |
| 40102 | `AccountLocked` | 账户已锁定（5 次失败后锁定 15 分钟） |
| 40104 | `InvalidToken` | 令牌无效或已过期 |
| 40105 | `InvalidRefreshToken` | 刷新令牌无效 |
| 40110 | `RefreshTokenAlreadyUsed` | 刷新令牌已被使用 |
| 40111 | `TokenRevoked` | 令牌已被注销 |

**全局过滤器豁免**（`config.json`）：`/api/auth/register`、`/api/auth/login`、`/api/auth/refresh` 三条路径豁免全局 JWT 和限流过滤器。`/api/auth/logout` **不在豁免列表中**，需要有效 JWT。

**桌面端关键行为**：

- Access Token 过期前应主动刷新，过期后若刷新也失败则引导重新登录
- 登录响应中的 `user` 对象可直接用于初始化用户资料 UI
- 并发场景下只允许一个刷新请求，避免 Refresh Token 竞态使用

---

## 3. 用户资料与存储统计

| 能力 | 路由 | 认证模式 | 返回形态 | 桌面端注意事项 |
|------|------|----------|----------|----------------|
| 获取用户信息 | `GET /api/user/profile` | JWT Bearer | `{ user: { id, username, email, nickname, avatar, storage_used, storage_quota, file_count, folder_count, created_at, updated_at } }` | 响应头可能包含 `ETag`，用于后续 PATCH 的乐观锁 |
| 更新用户信息 | `PATCH /api/user/profile` | JWT Bearer | `{ user: { ...完整用户对象 } }` | Merge Patch 语义：仅传 `nickname` 和/或 `avatar`，未传字段保持原值；显式 `null` 触发校验失败；支持可选 `If-Match` 乐观锁 |
| 修改密码 | `PUT /api/user/password` | JWT Bearer | `{}`（data 为 null） | 请求体 `{ old_password, new_password }`；修改成功后建议重新登录 |
| 获取存储空间统计 | `GET /api/user/storage` | JWT Bearer | `{ used, reserved, quota, percentage, categories }` | **三项配额语义**（见下方说明） |

### 3.1 存储配额三字段语义

| 字段 | 含义 | 来源 |
|------|------|------|
| `used` | 实际已使用空间（含回收站文件） | `SUM(files.size)` |
| `reserved` | 已预占用但未完成的上传空间 | `SUM(upload_tasks.file_size WHERE status=0)` |
| `quota` | 用户总存储配额 | `users.storage_quota`（默认 10 GB） |

**有效可用空间计算**：`可用 = quota - used - reserved`

**桌面端必须**：
- 上传前检查 `quota - used - reserved` 是否足够
- 理解 `reserved` 不为零是正常并发上传状态
- 回收站文件仍计入 `used`，仅彻底删除才释放

### 3.2 错误码族

| 错误码 | 枚举 | 说明 |
|--------|------|------|
| 10001 | `InvalidParameter` | 空请求体、URL 格式错误 |
| 10002 | `ValidationFailed` | 昵称长度错误、avatar 安全约束违反 |
| 10004 | `ResourceConflict` | `If-Match` 不匹配（乐观锁冲突） |

---

## 4. 文件浏览与搜索

| 能力 | 路由 | 认证模式 | 返回形态 | 桌面端注意事项 |
|------|------|----------|----------|----------------|
| 获取文件列表 | `GET /api/file/list` | JWT Bearer | 分页 `{ items, pagination }` | **混合负载**：`items` 同时包含文件和文件夹，通过 `type` 字段区分（`"file"` / `"folder"`） |
| 获取文件详情 | `GET /api/file/{file_id}` | JWT Bearer | `{ id, name, type, size, hash, mime_type, parent_id, path, created_at, updated_at }` | `type` 固定为 `"file"`；`path` 提供完整路径 |
| 搜索文件 | `GET /api/file/search` | JWT Bearer | 分页 `{ items, pagination }` | 混合负载同上；`items` 中每项额外含 `path` 字段；支持 `folder_id` 限定搜索范围 |
| 重命名 | `PUT /api/file/{file_id}/rename` | JWT Bearer | `{ id, name, updated_at }` | `file_id` **可为文件或文件夹 ID**，服务端自动判断类型 |
| 移动 | `PUT /api/file/move` | JWT Bearer | `{ moved_count }` | 批量操作：`file_ids` 可混合文件和文件夹 ID；部分失败时成功项仍完成 |
| 复制 | `POST /api/file/copy` | JWT Bearer | `{ copied_count, new_files: [{ old_id, new_id }] }` | 批量操作；复制前检查配额，超配额则整体拒绝（不部分执行） |
| 删除（移入回收站） | `DELETE /api/file` | JWT Bearer | `{ deleted_count }` | 软删除：移入回收站，**不释放存储空间**；`file_ids` 可混合文件和文件夹 ID |

### 4.1 列表接口查询参数

**`GET /api/file/list`**：

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `parent_id` | integer | 0（根目录） | 父文件夹 ID |
| `page` | integer | 1 | 页码 |
| `page_size` | integer | 20（最大 100） | 每页数量 |
| `sort_by` | string | — | 排序字段：`name` / `size` / `created_at` / `updated_at` |
| `sort_order` | string | `asc` | 排序方向 |
| `type` | string | `all` | 筛选类型：`all` / `file` / `folder` |

**`GET /api/file/search`**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `keyword` | string | 是 | 搜索关键词，1-100 字符 |
| `type` | string | 否 | `all` / `file` / `folder`，默认 `all` |
| `folder_id` | integer | 否 | 限定搜索范围，不指定则全局搜索 |
| `page` | integer | 否 | 页码，默认 1 |
| `page_size` | integer | 否 | 每页数量，默认 20，最大 100 |

### 4.2 混合负载说明

`items` 数组中文件与文件夹的字段差异：

| 字段 | 文件（`type: "file"`） | 文件夹（`type: "folder"`） |
|------|----------------------|--------------------------|
| `size` | ✅ 文件大小（字节） | ❌ 不存在 |
| `mime_type` | ✅ MIME 类型 | ❌ 不存在 |
| `hash` | ✅ MD5 哈希 | ❌ 不存在 |
| `item_count` | ❌ 不存在 | ✅ 子项数量 |

**桌面端**：列表模型必须根据 `type` 字段分发到不同的 delegate 或属性集。

### 4.3 错误码族

| 错误码 | 枚举 | 说明 |
|--------|------|------|
| 50001 | `InvalidFilename` | 文件名包含非法字符 |
| 50005 | `FileNotFound` | 文件不存在或不属于当前用户 |
| 50006 | `FolderNotFound` | 文件夹不存在 |
| 50007 | `FileAlreadyExists` | 同目录下同名文件已存在（重命名时同类型冲突） |
| 50010 | `FolderAlreadyExists` | 同目录下同名文件夹已存在 |

---

## 5. 文件夹树与面包屑

| 能力 | 路由 | 认证模式 | 返回形态 | 桌面端注意事项 |
|------|------|----------|----------|----------------|
| 创建文件夹 | `POST /api/folder/create` | JWT Bearer | `{ id, name, parent_id, path, created_at }` | 请求体 `{ name, parent_id }`；同名文件夹检测返回 `409 + 50010` |
| 获取目录树 | `GET /api/folder/tree` | JWT Bearer | 递归树 `{ id, name, children: [...] }` | 查询参数 `parent_id`（默认 0）、`depth`（默认 -1 全部展开）；用于左侧栏树形导航 |
| 获取面包屑 | `GET /api/folder/{folder_id}/breadcrumb` | JWT Bearer | `{ path: [{ id, name }, ...] }` | 路径从根目录到当前文件夹；用于顶部路径导航 |

**桌面端关键行为**：
- 目录树可懒加载（通过 `depth` 参数控制展开层级）
- 面包屑每一项可点击跳转到对应文件夹（使用 `id` 调用 `GET /api/file/list?parent_id=<id>`）

---

## 6. 上传能力

上传流程为四步会话：初始化 → 逐片上传 → 完成（或取消）。

| 能力 | 路由 | 认证模式 | 请求形态 | 返回形态 | 桌面端注意事项 |
|------|------|----------|----------|----------|----------------|
| 初始化上传 | `POST /api/file/upload/init` | JWT Bearer + 上传限流 | `{ filename, file_size, file_hash, parent_id }` | `{ file_id, filename, file_size, file_hash, mime_type, supports_range }` 或秒传结果 | **预占用语义**：`reserved += file_size`；秒传直接返回文件记录不经过预占用 |
| 上传分片 | `POST /api/file/upload/chunk` | JWT Bearer + 上传限流 | 查询参数 `upload_id`、`chunk_index`、`chunk_hash`；请求体 `application/octet-stream` | `{ chunk_index, uploaded: true }` | 默认分片 5 MB（`config.json` → `chunk_size: 5242880`）；每片校验 MD5 |
| 完成上传 | `POST /api/file/upload/complete` | JWT Bearer + 上传限流 | `{ upload_id }` | `{ file: { id, name, size, hash, mime_type, parent_id, created_at } }` | **配额转换**：`reserved -= file_size`，`used += file_size`；原子操作 |
| 取消上传 | `DELETE /api/file/upload/{upload_id}` | JWT Bearer + 上传限流 | 路径参数 `upload_id` | `{}`（data 为 null） | **释放预占用**：`reserved -= file_size`；清理临时文件 |

### 6.1 上传生命周期配额变化

| 阶段 | `used` | `reserved` | 说明 |
|------|--------|-----------|------|
| `init` | 不变 | `+file_size` | 预占用 |
| `chunk` | 不变 | 不变 | 逐片传输 |
| `complete` | `+file_size` | `-file_size` | 预占用转实际使用 |
| `cancel` | 不变 | `-file_size` | 释放预占用 |
| 秒传 | `+file_size` | 不变 | 跳过预占用，直接创建记录 |

### 6.2 桌面端关键行为

- 上传前计算文件 MD5 哈希，若命中秒传则跳过分片阶段
- 断点续传：重新调用 `init` 获取已有分片进度，从断点继续
- 上传任务有效期 24 小时（`config.json` → `upload_task_expiry_seconds: 86400`）
- 上传限流 240 次/分钟（`config.json` → `upload_rate_limit_per_minute: 240`）

### 6.3 错误码族

| 错误码 | 枚举 | 说明 |
|--------|------|------|
| 50004 | `StorageQuotaExceeded` | 存储空间不足 |
| 50008 | `UploadTaskNotFound` | 上传任务不存在或已过期 |
| 50009 | `ChunkVerifyFailed` | 分片校验失败（哈希不匹配） |

---

## 7. 下载能力

| 能力 | 路由 | 认证模式 | 返回形态 | 桌面端注意事项 |
|------|------|----------|----------|----------------|
| 获取下载信息 | `GET /api/file/download/{file_id}/info` | JWT Bearer | `{ file_id, filename, file_size, file_hash, mime_type, supports_range }` | 下载前预检：获取文件元数据用于 UI 展示和 Range 规划 |
| 下载文件 | `GET /api/file/download/{file_id}` | JWT Bearer | 二进制流（`Content-Type`、`Content-Disposition`、`Accept-Ranges: bytes`） | **支持 Range 请求**；支持断点续传 |

### 7.1 Range 下载语义

| 场景 | 请求头 | HTTP 状态码 | 响应 |
|------|--------|------------|------|
| 完整下载 | 无 Range | `200 OK` | 完整文件内容 |
| 范围下载 | `Range: bytes=0-1048575` | `206 Partial Content` | 指定字节范围 + `Content-Range` 头 |
| 范围无效 | `Range: bytes=999999000-`（超出大小） | `416 Range Not Satisfiable` | JSON 错误响应含 `file_size` 和 `requested_range` |

**桌面端**：下载大文件时应使用 Range 分片下载，中断后可从已下载位置续传。

### 7.2 错误码族

| 错误码 | 枚举 | 说明 |
|--------|------|------|
| 50005 | `FileNotFound` | 文件不存在或不属于当前用户 |
| 10002 | `ValidationFailed` | Range 范围无效 |

---

## 8. 分享管理（Owner）

本节所有接口使用所有者认证域：`Authorization: Bearer <access_token>`。

| 能力 | 路由 | 认证模式 | 请求形态 | 返回形态 | 桌面端注意事项 |
|------|------|----------|----------|----------|----------------|
| 创建分享 | `POST /api/share` | JWT Bearer | `{ file_ids, expire_days?, password?, permission? }` | `{ share_id, share_link, password, permission, expires_at, created_at }` | `file_ids` 可混合文件和文件夹 ID；文件夹分享采用快照语义 |
| 分享列表 | `GET /api/share` | JWT Bearer | 查询参数 `status?`、`page?`、`page_size?` | 分页 `{ items, pagination }` | `status` 筛选：`all` / `active` / `expired` / `cancelled` |
| 分享详情 | `GET /api/share/{share_id}` | JWT Bearer | 路径参数 `share_id`（外部标识符） | `{ share_id, files, share_link, has_password, permission, view_count, download_count, status, ... }` | `share_id` 是 `share_code`（如 `sh_abc123`），不是数据库自增 ID |
| 更新分享设置 | `PUT /api/share/{share_id}` | JWT Bearer | `{ expire_days?, password?, permission? }` | `{ share_id, expires_at, has_password, permission, updated_at }` | 空字符串 `password` 表示移除密码 |
| 取消分享 | `DELETE /api/share` | JWT Bearer | `{ share_ids: [...] }` | **部分成功批量响应**（见下方） | HTTP 始终 200；通过 `summary` 和 `results` 表达每项结果 |

### 8.1 取消分享 — 部分成功语义

```json
{
  "code": 0,
  "data": {
    "summary": { "total": 3, "succeeded": 1, "failed": 2 },
    "results": [
      { "share_id": "sh_abc123", "status": "success" },
      { "share_id": "sh_bad1", "status": "failed", "error": { "code": 60001, "message": "分享不存在" } },
      { "share_id": "sh_bad2", "status": "failed", "error": { "code": 60002, "message": "分享已过期" } }
    ]
  }
}
```

**桌面端**：必须解析 `results` 数组逐项展示成功/失败状态，不能仅依赖 `summary.succeeded == summary.total`。

### 8.2 错误码族

| 错误码 | 枚举 | 说明 |
|--------|------|------|
| 60001 | `ShareNotFound` | 分享不存在或不属于当前用户 |
| 60002 | `ShareExpired` | 分享已过期 |
| 60004 | `ShareAccessDenied` | 无权限访问 |

---

## 9. 分享访问（Visitor）

本节描述访客认证域流程。访客 **不需要注册或登录**，认证链为：公开获取 Share Token → 使用 `X-Share-Token` 头访问浏览和下载。

### 9.1 访客流程

```
1. 提取 share_id（从分享链接）
2. POST /api/share/access/{share_id}  →  获取 share_token
3. GET  /api/share/browse/{share_id}  ←  X-Share-Token
4. GET  /api/share/download/{share_id}/{file_id}  ←  X-Share-Token
```

### 9.2 接口映射

| 能力 | 路由 | 认证模式 | 请求形态 | 返回形态 | 桌面端注意事项 |
|------|------|----------|----------|----------|----------------|
| 验证分享访问 | `POST /api/share/access/{share_id}` | **公开**（豁免全局过滤器） | `{ password? }` | `{ share_token, expires_in, permission, files }` | 若分享设有密码则必须传入；`expires_in` 默认 3600 秒 |
| 浏览分享内容 | `GET /api/share/browse/{share_id}` | `X-Share-Token` | 查询参数 `folder_id?` | `{ items: [...], breadcrumb: [...] }` | `X-Share-Token` 头；`items` 混合文件/文件夹；支持文件夹层级浏览 |
| 下载分享文件 | `GET /api/share/download/{share_id}/{file_id}` | `X-Share-Token` | 路径参数 `share_id`、`file_id`；可选 `Range` 头 | 二进制流 | **支持 Range 请求**（与 Owner 下载相同语义）；`permission: "view"` 时返回 `403 + 60004` |

### 9.3 认证过滤器行为

- **`/api/share/access/{share_id}`**：`config.json` GlobalFilters 豁免，且控制器未注册任何显式过滤器 → **完全公开**
- **`/api/share/browse/{share_id}`**：GlobalFilters 豁免，但控制器注册 `ShareAuthFilter` → **需要 `X-Share-Token`**
- **`/api/share/download/{share_id}/{file_id}`**：同上

`ShareAuthFilter` 行为（`src/filters/ShareAuthFilter.cpp`）：
1. 读取 `X-Share-Token` 头
2. 调用 `TokenService::VerifyShareTokenWithRedis` 验证
3. 将 `share_code` 和 `share_id` 写入请求 attributes
4. 过期/撤销/格式错误分别返回 `TokenExpired` / `TokenRevoked` / `TokenMalformed`

### 9.4 错误码族

| 错误码 | 枚举 | 说明 |
|--------|------|------|
| 60001 | `ShareNotFound` | 分享不存在或已被取消 |
| 60002 | `ShareExpired` | 分享已过期 |
| 60003 | `SharePasswordError` | 分享密码错误（含暴力锁定提示） |
| 60004 | `ShareAccessDenied` | 分享设置为仅查看，不允许下载 |

---

## 10. 回收站能力

| 能力 | 路由 | 认证模式 | 请求形态 | 返回形态 | 桌面端注意事项 |
|------|------|----------|----------|----------|----------------|
| 回收站列表 | `GET /api/trash` | JWT Bearer | 查询参数 `page?`、`page_size?` | 分页 `{ items, pagination }` | 每项含 `original_id`、`original_path`、`deleted_at`、`expires_at`（30 天后自动清理） |
| 恢复 | `POST /api/trash/restore` | JWT Bearer | `{ trash_ids: [...] }` | **部分成功批量响应** `{ summary, results }` | 原始位置不存在时恢复到根目录；同名冲突自动重命名 `name (n).ext` |
| 彻底删除 | `DELETE /api/trash` | JWT Bearer | `{ trash_ids: [...] }` | **部分成功批量响应** `{ summary, results }` | 彻底删除 **释放存储空间**；每项 `results[]` 含 `freed_space` |
| 清空回收站 | `DELETE /api/trash/all` | JWT Bearer | 无请求体 | `{ deleted_count, freed_space }` | 一次性清空所有回收站项目 |

### 10.1 部分成功批量响应

恢复和彻底删除均采用部分成功语义：

```json
{
  "summary": { "total": 3, "success_count": 2, "failure_count": 1 },
  "results": [
    { "trash_id": 1, "status": "success", "file_id": 123, "path": "/文档/已删除文件.pdf" },
    { "trash_id": 2, "status": "success", "freed_space": 102400 },
    { "trash_id": 3, "status": "failed", "error": { "code": 10003, "message": "资源不存在" } }
  ]
}
```

**桌面端**：必须逐项解析 `results`，展示每项操作结果。

### 10.2 错误码族

| 错误码 | 枚举 | 说明 |
|--------|------|------|
| 10003 | `ResourceNotFound` | trash_id 不存在或不属于用户 |

---

## 11. 系统与日志接口

| 能力 | 路由 | 认证模式 | 返回形态 | 桌面端注意事项 |
|------|------|----------|----------|----------------|
| 健康检查 | `GET /api/health` | **公开** | `{ overall_status, version, uptime, timestamp, components: { database, redis } }` | 无需认证；`overall_status` 为 `healthy` / `degraded` / `unhealthy`；503 表示不健康 |
| 系统信息 | `GET /api/system/info` | JWT Bearer | `{ version, drogon_version, build_time, uptime, connections, storage }` | 包含全局统计（用户数、文件数、总大小）；用于管理/关于页面 |
| 操作日志 | `GET /api/logs` | JWT Bearer | 分页 `{ items, total, page, page_size }` | `items[].action` 类型：`login`、`logout`、`upload`、`download`、`delete`、`rename`、`move`、`copy`、`share`、`restore` |

**桌面端关键行为**：
- 健康检查可用于应用启动时验证后端连通性
- 操作日志的分页结构与文件列表不同（直接在 `data` 层级包含 `total`、`page`、`page_size`，而非嵌套 `pagination` 对象）

---

## 12. 客户端实现注意事项

### 12.1 认证域隔离

| 规则 | 说明 |
|------|------|
| 令牌不可混用 | Owner JWT 不能用于 Share 接口，Share Token 不能用于 Owner 接口 |
| 过滤器分离 | `JwtAuthFilter` 校验 `Authorization: Bearer`；`ShareAuthFilter` 校验 `X-Share-Token` |
| 状态机管理 | 桌面端通过全局状态机区分当前活跃认证域，UI 层据此切换导航和功能入口 |

### 12.2 混合负载处理

| 场景 | 接口 | 处理方式 |
|------|------|----------|
| 文件列表 | `GET /api/file/list` | `items[]` 中 `type` 字段区分文件/文件夹，字段集不同 |
| 搜索结果 | `GET /api/file/search` | 同上，额外包含 `path` 字段 |
| 分享浏览 | `GET /api/share/browse/{share_id}` | 同上 |
| 重命名/移动/复制/删除 | 文件操作接口 | `file_id` / `file_ids` 可为文件或文件夹 ID，服务端自动判断 |

### 12.3 批量操作部分成功

以下接口采用部分成功语义，桌面端 **必须** 逐项解析：

| 接口 | 说明 |
|------|------|
| `DELETE /api/share` | 取消分享，`summary + results[]` |
| `POST /api/trash/restore` | 恢复回收站，`summary + results[]` |
| `DELETE /api/trash` | 彻底删除，`summary + results[]` |

### 12.4 下载 Range 支持

所有者下载和访客下载均支持 HTTP Range 请求：

| 接口 | Range 支持 |
|------|-----------|
| `GET /api/file/download/{file_id}` | ✅ `Authorization: Bearer` |
| `GET /api/share/download/{share_id}/{file_id}` | ✅ `X-Share-Token` |

### 12.5 限流配置

| 接口类型 | 限流规则 |
|----------|----------|
| 认证接口 | 10 次/分钟/IP |
| 普通接口 | 100 次/分钟/用户 |
| 上传接口 | 240 次/分钟/用户（`config.json` 配置） |
| 分享访问 | 30 次/分钟/IP |

限流触发时返回 `429 + 10005 TooManyRequests`，响应体含 `retry_after` 字段。

### 12.6 分页参数默认值

| 接口 | `page` 默认 | `page_size` 默认 | `page_size` 上限 |
|------|------------|-----------------|-----------------|
| 文件列表 | 1 | 20 | 100 |
| 搜索 | 1 | 20 | 100 |
| 分享列表 | 1 | 20 | — |
| 回收站列表 | 1 | 20 | — |
| 操作日志 | 1 | 20 | 100 |

---

## 13. 参考资料

| 文档 | 路径 |
|------|------|
| 桌面端范围与成功标准 | `docs/desktop/00-scope-and-success.md` |
| API 接口设计（权威） | `docs/design/02-API接口设计.md` |
| 数据库设计 | `docs/design/03-数据库设计.md` |
| 认证控制器 | `src/controllers/AuthController.hpp` |
| 用户控制器 | `src/controllers/UserController.hpp` |
| 文件控制器 | `src/controllers/FileController.hpp` |
| 文件夹控制器 | `src/controllers/FolderController.hpp` |
| 分享控制器 | `src/controllers/ShareController.hpp` |
| 回收站控制器 | `src/controllers/TrashController.hpp` |
| 健康检查控制器 | `src/controllers/HealthController.hpp` |
| 系统控制器 | `src/controllers/SystemController.hpp` |
| 操作日志控制器 | `src/controllers/OperationLogController.hpp` |
| JWT 过滤器 | `src/filters/JwtAuthFilter.cpp` |
| 分享过滤器 | `src/filters/ShareAuthFilter.cpp` |
| 运行时配置 | `config.json` |
