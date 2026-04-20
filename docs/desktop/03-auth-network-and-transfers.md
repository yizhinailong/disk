# 桌面客户端 — 认证、网络与传输状态机

## 1. 文档说明

### 1.1 文档目的

本文档冻结桌面客户端在 **Owner Session**、**Visitor Session**、上传、下载四条主流程中的状态机与网络规则。目标是把后端已经实现的过滤器、令牌校验、上传生命周期、Range 下载语义，转成桌面端可直接实施的状态边界，避免后续实现阶段出现“Owner/Visitor 混用令牌”“并发刷新打爆 refresh token”“上传完成后配额显示错误”等问题。

### 1.2 权威来源

| 来源 | 路径 | 用途 |
|------|------|------|
| 桌面端范围基线 | `docs/desktop/00-scope-and-success.md` | Owner / Visitor 双认证域边界 |
| 路由与能力基线 | `docs/desktop/01-backend-capability-map.md` | 路由、错误码、基础流程 |
| 领域模型与错误分类 | `docs/desktop/02-domain-models-and-errors.md` | `OwnerSession`、`ShareVisitorSession`、`UploadTask`、`DownloadTask` |
| 全局过滤器 | `config.json` | 公开路由、全局 `JwtAuthFilter` / `RateLimitFilter` 豁免规则 |
| Owner 认证过滤器 | `src/filters/JwtAuthFilter.cpp` | `Authorization: Bearer` 校验、撤销检查 |
| Visitor 认证过滤器 | `src/filters/ShareAuthFilter.cpp` | `X-Share-Token` 校验、`share_id` 注入 |
| 令牌服务 | `src/services/TokenService.cpp` | Access/Refresh/Share Token 生成、校验、撤销、Redis CAS |
| 认证服务 | `src/services/AuthService.cpp` | 登录、刷新、登出 |
| 文件服务 | `src/services/FileService.cpp` | 上传任务、`reserved -> used` 转换、下载元数据 |
| 分享服务 | `src/services/ShareService.cpp` | 访客 access/browse/download、权限校验 |
| 过期清理服务 | `src/services/CleanupService.cpp` | 上传任务过期后的 `reserved` 释放 |
| 下载响应器 | `src/controllers/DownloadResponder.cpp` | `200/206/416`、`Content-Range`、`Accept-Ranges` |

### 1.3 不变量

1. **Owner Session** 与 **Visitor Session** 是两个不同状态机，令牌绝不混用。
2. 桌面端只允许一个活动认证域；进入另一个认证域前必须先结束当前域的网络上下文。
3. `single-flight` 刷新是强约束：同一时刻只能存在一个 access token 刷新请求。
4. 上传配额显示必须区分 `reserved` 与 `used`；二者不可合并成单一“已用空间”。
5. 下载必须显式建模 `200`、`206`、`416`，不能把所有非 `200` 都当成失败重试。

---

## 2. 网络层总规则

### 2.1 请求管线与令牌注入

| 请求类型 | 客户端头注入 | 服务器过滤链 | 桌面端规则 |
|------|------|------|------|
| `POST /api/auth/register` | 无 | GlobalFilters 豁免 | 公开请求，不注入 Bearer |
| `POST /api/auth/login` | 无 | GlobalFilters 豁免 | 公开请求，不注入 Bearer |
| `POST /api/auth/refresh` | 无；token 在 JSON body `refresh_token` | GlobalFilters 豁免 | 只能由 C++ 会话层触发，QML 不直接调用 |
| Owner 受保护请求（含 `/api/auth/logout`） | `Authorization: Bearer <access_token>` | GlobalFilters → `JwtAuthFilter` → `RateLimitFilter` / `UploadRateLimitFilter` → Controller | 使用统一 bearer-token injection，不允许手工拼头 |
| `POST /api/share/access/{share_id}` | 无；密码在 JSON body | GlobalFilters 豁免 | 公开请求；不得附带 Bearer |
| Visitor 浏览/下载 | `X-Share-Token: <share_token>` | GlobalFilters 豁免 → `ShareAuthFilter` → `ShareController` 路径绑定校验 | 只能注入 `X-Share-Token`，不得附带 `Authorization` |

### 2.2 Owner 请求管线细则

`JwtAuthFilter.cpp` 的实际管线如下：

| 顺序 | 服务器动作 | 失败结果 | 客户端含义 |
|------|------|------|------|
| 1 | 读取 `Authorization` | `40106 TokenMissing` | 本地请求工厂失配或本地会话已清空 |
| 2 | 检查是否以 `Bearer ` 开头 | `40107 TokenMalformed` | 请求头拼装错误，不做自动 retry |
| 3 | `TokenService::VerifyAccessToken` 校验 JWT 与 type=access | `40108 TokenExpired` / `40104 InvalidToken` / `40109 TokenWrongType` | 进入 Owner 刷新判定 |
| 4 | `IsAccessTokenRevoked(jti)` | `40111 TokenRevoked` | Access Token 已黑名单化 |
| 5 | 写入 `user_id`、`username` attributes | 成功进入 Controller | 说明业务逻辑尚未运行前的认证已通过 |

**请求重放规则**：

1. 只有当请求已经收到 **明确的 401 响应**，并且该 401 来自认证链时，才允许在刷新成功后重放一次。
2. 由于 `JwtAuthFilter` 运行在 Controller 之前，因认证失败返回的 `401` 代表业务逻辑尚未执行，因此该请求可安全重放一次。
3. **transport failure / network drop** 没有服务端响应时，不做“全局自动重放”；是否 retry 由具体端点的幂等性决定。

### 2.3 Visitor 请求管线细则

`ShareAuthFilter.cpp` + `ShareController.cpp` 的实际 Visitor 管线如下：

| 顺序 | 服务器动作 | 失败结果 | 客户端含义 |
|------|------|------|------|
| 1 | 读取 `X-Share-Token` | `40106 TokenMissing` | VisitorSession 本地令牌缺失 |
| 2 | `VerifyShareTokenWithRedis` 校验 JWT、校验撤销 | `40108 TokenExpired` / `40111 TokenRevoked` / `40107 TokenMalformed` | 不能走 Owner refresh，必须重新验证分享 |
| 3 | 将 `share_code`、内部 `share_id` 写入 request attributes | 成功进入 Controller | 访客令牌已通过过滤器 |
| 4 | `ShareController` 校验 URL 中的 `{share_id}` 是否等于 token 内 `share_code` | `403 60004 ShareAccessDenied` | 当前 token 只能用于它所属的分享路径 |

### 2.4 客户端网络层建议约束

| 约束 | 说明 |
|------|------|
| 一个 C++ 网络层 | 统一封装 base URL、序列化、错误归一化、请求取消、状态机回调 |
| 一个预配置请求工厂 | Owner 工厂负责 bearer injection；Visitor 工厂负责 `X-Share-Token` 注入 |
| QML 只消费状态 | QML 只看 `SessionStore`、`TransferStore`、`ApiError`，不直接拼请求头、不直接做 refresh |
| 请求必须带域信息 | 每个请求在本地都有 `auth_domain=owner/visitor/public`，防止 token 注入到错误域 |

---

## 3. Owner Session State Machine

### 3.1 状态集合

| 状态 | 含义 | 允许动作 |
|------|------|------|
| `LoggedOut` | 本地无有效 `OwnerSession` | register / login |
| `Authenticating` | 正在登录 | 等待登录响应，不发 owner 业务请求 |
| `Active` | 已持有 `access_token + refresh_token` | 所有 Owner API |
| `Refreshing` | 正在执行 `single-flight` access-token refresh | 新到达的 401 请求排队等待，不再发第二个 refresh |
| `ReauthRequired` | refresh 失败或 token 不可恢复 | 清理会话并跳回登录 |
| `LogoutPending` | 用户明确登出 | 停止调度新 owner 请求，执行本地清理 |

### 3.2 状态迁移表

| 当前状态 | 触发条件 | 下一状态 | 桌面端动作 |
|------|------|------|------|
| `LoggedOut` | 登录成功 | `Active` | 持久化 access/refresh token，初始化用户资料与目录首页 |
| `Active` | 收到 `40108 TokenExpired` / `40104 InvalidToken` | `Refreshing` | 启动 `single-flight` refresh |
| `Refreshing` | refresh 成功 | `Active` | 原子替换 access_token + refresh_token，唤醒等待请求并各自 replay 一次 |
| `Refreshing` | `40105 InvalidRefreshToken` / `40110 RefreshTokenAlreadyUsed` / `40111 TokenRevoked` / `40107 TokenMalformed` | `ReauthRequired` | 清空本地 owner 令牌与缓存，停止 replay |
| `Active` | 用户点“登出” | `LogoutPending` | 不再触发 refresh；发起 best-effort logout，并开始本地清理 |
| `LogoutPending` | 本地清理完成 | `LoggedOut` | 返回登录页 |

### 3.3 single-flight access-token refresh 规则

`AuthService.cpp` + `TokenService.cpp` 明确显示 refresh token 是**单次使用**，并通过 Redis Compare-And-Swap 保证只有一个刷新成功；并发刷新会得到 `40110 RefreshTokenAlreadyUsed`。因此客户端必须遵守以下 `single-flight` 规则：

| 规则 | 必须执行的客户端策略 |
|------|------|
| 只允许一个 refresh 在飞 | `Refreshing` 状态下，其它 401 请求只能等待同一个 future/promise 结果 |
| 不能并行消耗旧 refresh token | refresh 发起后，本地把当前 refresh token 标记为“consumed in-flight”，直到成功替换或失败清空 |
| refresh 成功后原子替换 token 对 | 必须同时替换 `access_token` 与 `refresh_token`；不能只更新 access token |
| 每个原请求最多 replay 一次 | 防止 `401 -> refresh -> replay -> 401 -> refresh` 死循环 |
| 用户显式登出时禁止 refresh | `LogoutPending` 期间收到 401 直接走本地清理，不再刷新 |

### 3.4 Owner 请求 replay 决策表

| 场景 | 是否自动 replay | 上限 | 说明 |
|------|------|------|------|
| 认证链返回 `40108 TokenExpired` | 是 | refresh 成功后 replay 1 次 | 业务逻辑尚未执行 |
| 认证链返回 `40111 TokenRevoked` | 否 | 0 | 直接进入 `ReauthRequired` |
| `POST /api/auth/refresh` 本身失败 | 否 | 0 | 不再套 refresh 的 refresh |
| 普通请求 `network drop` 且无 HTTP 响应 | 否（默认） | 0 | 交给端点级幂等策略；不能全局盲目 replay |
| `GET` 列表/详情类 `5xx` | 是 | 最多 3 次 retry | 属于读请求，可退避重试 |

### 3.5 登出 cleanup 规则

| 步骤 | 说明 |
|------|------|
| 1 | 停止发出新的 Owner 请求，冻结等待队列 |
| 2 | 对当前 access token 执行 `POST /api/auth/logout`（不触发 refresh） |
| 3 | 无论 logout 成功、返回 `401`，还是发生 `network drop`，都继续本地清理；登出是用户显式意图，不应被网络失败阻塞 |
| 4 | 清空 `access_token`、`refresh_token`、用户资料缓存、目录缓存、分享管理缓存、等待中的 `single-flight` refresh 句柄 |
| 5 | 终止仍在执行的 Owner 网络请求；活跃上传/下载任务不得自动迁移到下一个账户 |

---

## 4. Share Visitor State Machine

### 4.1 状态集合

| 状态 | 含义 | 允许动作 |
|------|------|------|
| `Idle` | 尚未打开分享 | 解析分享链接 |
| `Unverified` | 已得到 `share_id`，尚无 `share_token` | 输入密码（如有）、调用 access |
| `Verifying` | 正在 `POST /api/share/access/{share_id}` | 等待 `share_token` |
| `Active` | 已持有 `share_token`，可 browse/download | 浏览、Range 下载、重新进入子目录 |
| `ReverifyRequired` | `share_token` 过期/撤销/损坏 | 重新 `POST /api/share/access/{share_id}` |
| `Closed` | 用户关闭分享视图 | 清空 VisitorSession |

### 4.2 Share Token 生命周期

| 阶段 | 服务端事实 | 客户端行为 |
|------|------|------|
| 获取 | `POST /api/share/access/{share_id}` 返回 `share_token`、`expires_in=3600`、`permission`、根级 `files` | 建立 `ShareVisitorSession` |
| 使用 | 浏览/下载必须携带 `X-Share-Token` | 统一使用 Visitor 请求工厂注入 |
| 路径绑定 | `ShareController` 会比对 URL `share_id` 与 token 中 `share_code` | 不允许把一个分享的 token 带到另一个分享路径 |
| 过期 | `ShareAuthFilter` 返回 `40108 TokenExpired` | 清空旧 token，进入 `ReverifyRequired` |
| 撤销 | `ShareAuthFilter` 返回 `40111 TokenRevoked` | 清空旧 token，进入 `ReverifyRequired` 或关闭分享 |
| 关闭 | 服务端无“访客登出”接口 | 本地仅清理 `share_id/share_token/permission/shared_items` |

### 4.3 状态迁移表

| 当前状态 | 触发条件 | 下一状态 | 桌面端动作 |
|------|------|------|------|
| `Idle` | 打开分享链接 | `Unverified` | 提取 `share_id` |
| `Unverified` | access 成功 | `Active` | 保存 `share_token`、`permission`、根级列表 |
| `Unverified` | `60003 SharePasswordError` | `Unverified` | 保持在密码输入态，不清空 `share_id` |
| `Active` | browse/download 返回 `40108 TokenExpired` / `40111 TokenRevoked` / `40107 TokenMalformed` | `ReverifyRequired` | 清空 `share_token`，提示重新验证 |
| `Active` | download 返回 `403 60004 ShareAccessDenied` | `Active` | 保持浏览会话，禁用下载按钮，不进入失效态 |
| `Active` | 分享关闭或分享不存在（`60001/60002`） | `Closed` | 关闭分享视图 |
| `ReverifyRequired` | 再次 access 成功 | `Active` | 用新 token 全量替换旧会话 |

### 4.4 Visitor 特殊规则

1. Visitor 流**没有 refresh token**，因此绝不走 Owner 的 `single-flight` 刷新逻辑。
2. 访客浏览与下载虽然被 GlobalFilters 豁免，但并非公开接口；真正的保护点是 `ShareAuthFilter` + `ShareController` 对 `share_code` 的二次匹配。
3. `permission=view` 时，会话仍然有效，只是下载动作会返回 `403 60004 ShareAccessDenied`。

---

## 5. 所有者会话 vs 访客会话决策表

### 5.1 Owner / Visitor 行为对照

| 场景 | Owner Session | Visitor Session |
|------|------|------|
| 认证头 | `Authorization: Bearer <access_token>` | `X-Share-Token: <share_token>` |
| 令牌获取 | login / refresh | `POST /api/share/access/{share_id}` |
| 令牌过期 (`TokenExpired`) | 进入 `single-flight` refresh，成功后 replay 原请求 | 清空 share token，重新 access，不做静默 refresh |
| 令牌撤销 (`TokenRevoked`) | 清空 OwnerSession，跳登录页 | 清空 VisitorSession，回到分享验证入口 |
| 令牌格式错误 (`TokenMalformed`) | 视为本地会话损坏，清空并重新登录 | 视为本地分享会话损坏，重新 access |
| 业务 403 | 常见为权限不足/实现错误，不自动 retry | `ShareAccessDenied` 时保持浏览、禁止下载 |
| 显式退出 | 调 `/api/auth/logout` + 本地 cleanup | 仅本地 cleanup，无服务端 logout |

### 5.2 错误到动作决策表

| 条件 | Owner Session 动作 | Visitor Session 动作 |
|------|------|------|
| `40108 TokenExpired` | refresh 并 replay 1 次 | 进入 `ReverifyRequired` |
| `40105 InvalidRefreshToken` | 立即 `ReauthRequired` | 不适用 |
| `40110 RefreshTokenAlreadyUsed` | 立即 `ReauthRequired`，提示重新登录 | 不适用 |
| `40111 TokenRevoked` | 清空 OwnerSession | 清空 VisitorSession |
| `403 60004 ShareAccessDenied` | 不适用 | 浏览继续、下载禁用 |
| `404 60001 ShareNotFound / 60002 ShareExpired` | 不适用 | 关闭分享入口 |

---

## 6. Upload State Machine

### 6.1 状态集合

| 状态 | 含义 |
|------|------|
| `Queued` | 本地待处理，尚未计算哈希 |
| `Hashing` | 本地计算 MD5，用于秒传与断点续传 |
| `Initializing` | 调用 `POST /api/file/upload/init` |
| `InstantUploaded` | 命中秒传，上传直接完成 |
| `Resuming` | init 返回已有 `upload_id + uploaded_chunks` |
| `Uploading` | 逐片上传中 |
| `Completing` | 调用 `POST /api/file/upload/complete` |
| `CancelPending` | 调用 `DELETE /api/file/upload/{upload_id}` |
| `Completed` | 上传完成，`reserved -> used` 已转换 |
| `Cancelled` | 服务端确认取消，`reserved` 已释放 |
| `Expired` | 服务端任务过期或缺失，旧 `upload_id` 不再可用 |
| `Failed` | 超出 retry 边界或遇到不可恢复错误 |

### 6.2 状态迁移表

| 当前状态 | 触发条件 | 下一状态 | 说明 |
|------|------|------|------|
| `Queued` | 开始上传 | `Hashing` | 本地先算 MD5 |
| `Hashing` | 哈希完成 | `Initializing` | 发 init |
| `Initializing` | `instant_upload=true` | `InstantUploaded` -> `Completed` | 秒传不走 chunk 流程 |
| `Initializing` | 返回 `upload_id + uploaded_chunks` 且是已有任务 | `Resuming` -> `Uploading` | 仅补传缺失分片 |
| `Initializing` | 创建新任务成功 | `Uploading` | 从 chunk 0 开始 |
| `Uploading` | 所有 chunk 已上传 | `Completing` | 发 complete |
| `Uploading` | 用户点 cancel | `CancelPending` | 发 cancel 请求 |
| `Uploading` / `Completing` | `50008 UploadTaskNotFound` | `Expired` | 旧任务不存在或已过期 |
| `Completing` | complete 成功 | `Completed` | `reserved` 转 `used` |
| `CancelPending` | cancel 成功 | `Cancelled` | 释放 `reserved` |

### 6.3 服务端保证的上传生命周期

| 阶段 | 服务端行为 | 客户端可依赖的事实 |
|------|------|------|
| init（新任务） | `ReserveStorageQuota` 执行 `storage_reserved += file_size`；创建 `upload_tasks.status=0`；设置 `expires_at=now+86400s` | 成功 init 后，整文件大小已计入 `reserved` |
| init（秒传） | 直接创建文件记录，不预占 `storage_reserved` | `used` 不因秒传额外增加；`reserved=0` |
| init（续传） | 若发现同 hash 且未过期的现有任务，返回已有 `upload_id` 与 `uploaded_chunks` | 客户端必须按服务端返回的 chunk 列表恢复，而不是信任本地旧记录 |
| chunk | 校验 `chunk_index`、校验 `chunk_hash`、写临时文件、`INSERT IGNORE upload_task_chunks` | 同一 chunk 可 retry；幂等成功 |
| complete | 校验分片覆盖、组装文件、校验整文件 MD5、落库后执行 `storage_reserved -= file_size` 且 `storage_used += file_size` | 配额转换在 complete 成功后发生 |
| cancel | `storage_reserved -= reserved_bytes`，`status=2`，清理 chunk 记录与临时目录 | cancel 成功后应立即从 UI 移除 `reserved` |
| expire | `CleanupService` 将 `status=3`，释放 `storage_reserved`，清理临时资源 | 过期由服务端后台完成；客户端只在下次访问时观察到 |

### 6.4 `reserved` vs `used` 在上传中的展示规则

#### 6.4.1 语义表

| 上传阶段 | `reserved` | `used` | UI 解释 |
|------|------|------|------|
| init 前 | 不变 | 不变 | 文件尚未占用配额 |
| init 成功（新任务） | `+file_size` | 不变 | 该文件占用的是“预留空间” |
| chunk 进行中 | 不变 | 不变 | 进度增长不代表 `used` 增长 |
| complete 成功 | `-file_size` | `+file_size` | 预留空间转换为真实占用 |
| cancel 成功 | `-file_size` | 不变 | 释放预留，不产生已用空间 |
| 任务 expire | `-file_size` | 不变 | 后台清理释放预留 |
| 秒传 | 不变 | 不增加新的物理占用 | 从桌面体验看文件“已完成”，但不经过 `reserved` |

#### 6.4.2 客户端展示公式

后端代码与设计文档都维护 `storage_reserved`，但 **当前 `UserService.cpp` + `UserDto.hpp` 仍只序列化 `used/quota`，未把 `reserved` 放进响应 JSON**。因此桌面端必须按以下规则展示：

| 展示字段 | 规则 |
|------|------|
| `display_used` | 使用服务端最近一次已知的 `used` |
| `local_reserved_overlay` | 本地所有处于 `Initializing` / `Uploading` / `Completing` / `CancelPending` 的任务 `file_size` 求和 |
| `display_reserved` | 若 `/api/user/storage` 已返回 `reserved`，取 `max(server_reserved, local_reserved_overlay)`；若当前响应缺少 `reserved`，直接取 `local_reserved_overlay` |
| `display_available` | `max(0, quota - display_used - display_reserved)` |

这一定义把**服务端配额真实语义**与**当前序列化缺口**分开：服务端真实状态以 `storage_reserved/storage_used` 为准，桌面端显示层用本地 overlay 防止上传中错误地夸大可用空间。

### 6.5 上传 retry 与 cancel 语义

| 场景 | 客户端策略 | 边界 |
|------|------|------|
| `network drop` 发生在 init 后 | 重新调用 init；服务端会返回 resume 或新任务 | 最多 retry 3 次 |
| `network drop` 发生在 chunk 上传中 | 重试同一个 `chunk_index + chunk_hash` | 最多 retry 3 次；超过后任务进入 `Failed` |
| `50009 ChunkVerifyFailed` | 重算当前 chunk 哈希并重传当前 chunk | 当前 chunk 最多 retry 3 次 |
| `50008 UploadTaskNotFound` | 进入 `Expired`，清空旧 `upload_id`，重新 init | 不对旧 `upload_id` 做 retry |
| `429 TooManyRequests` | 读取 `X-RateLimit-Reset`，等待窗口重置后 retry | 同一阶段最多 retry 3 次 |
| 用户 cancel | 进入 `CancelPending`，直到收到 cancel 成功或确认任务已不存在 | cancel 请求本身可 retry，因为服务端对终态任务返回成功或视作已终止 |

### 6.6 续传恢复规则

1. 本地恢复上传时，必须再次调用 init，不能直接复用本地缓存的 `upload_id`。
2. 若服务端返回 `uploaded_chunks`，客户端只补传缺失分片。
3. 若服务端已清理旧任务并返回新任务，则旧 partial 记录全部丢弃，从新 `upload_id` 重新开始。

---

## 7. Download State Machine

### 7.1 状态集合

| 状态 | 含义 |
|------|------|
| `Idle` | 尚未获取下载上下文 |
| `FetchingMetadata` | 获取下载元数据或准备续传偏移 |
| `Ready` | 已得到文件名、目标路径、起始偏移 |
| `TransferringFull` | 无 `Range`，等待 `200 OK` |
| `TransferringRange` | 带 `Range`，等待 `206 Partial Content` |
| `Paused` | 本地停止 reply，保留 partial 文件 |
| `RetryWaiting` | 满足 retry 条件，等待退避 |
| `Completed` | 文件完整写入 |
| `Cancelled` | 用户显式取消，partial 文件已删除 |
| `Failed` | 不可恢复错误或超出 retry 边界 |

### 7.2 元数据准备规则

| 认证域 | 元数据来源 | 客户端动作 |
|------|------|------|
| Owner | `GET /api/file/download/{file_id}/info` 返回 `file_hash`、`mime_type`、`supports_range=true` | 先进入 `FetchingMetadata`，再决定 full / range |
| Visitor | 无单独 `/info` 路由；基础文件名/大小来自 share access/browse，真正的权限与存储路径在下载请求时由服务端内部校验 | 先从 `ShareVisitorSession` 读取展示信息，再直接发下载请求 |

### 7.3 状态迁移表

| 当前状态 | 触发条件 | 下一状态 | 说明 |
|------|------|------|------|
| `Idle` | 用户点击下载 | `FetchingMetadata` | Owner 发 `/info`；Visitor 准备本地上下文 |
| `FetchingMetadata` | 无 partial 文件 | `Ready` -> `TransferringFull` | 期望 `200 OK` |
| `FetchingMetadata` | 有 partial 文件且 `supports_range=true` | `Ready` -> `TransferringRange` | 请求 `Range: bytes=<received>-`，期望 `206` |
| `TransferringFull` / `TransferringRange` | 用户点 pause | `Paused` | abort 当前 reply，但保留 partial 文件 |
| `Paused` | 用户点 resume | `FetchingMetadata` | 重新计算偏移，再走 range/full |
| `TransferringFull` / `TransferringRange` | 用户点 cancel | `Cancelled` | abort reply，并删除 partial 文件 |
| `TransferringRange` | 收到 `416` | `FetchingMetadata` 或 `Failed` | 先纠正本地偏移；若仍异常则失败 |
| 任意传输态 | `network drop` / `5xx` | `RetryWaiting` | 仅在 retry 边界内继续 |
| 任意传输态 | 下载完成且哈希/字节数匹配 | `Completed` | 结束任务 |

### 7.4 `200 / 206 / 416` 处理规则

| HTTP 状态 | 含义 | 桌面端动作 |
|------|------|------|
| `200 OK` | 服务端返回完整文件流 | 从 0 开始覆盖写入；如果本地存在 partial 文件，应先清理或重命名 |
| `206 Partial Content` | 服务端接受 `Range`，响应 `Content-Range` | 从已有字节偏移继续写入；更新 `received_bytes` |
| `416 Range Not Satisfiable` | 本地请求偏移超出服务端文件大小 | 读取错误体中的 `file_size/requested_range`；删除或截断错误 partial 文件，然后从 0 重新进入 `FetchingMetadata` |

### 7.5 pause / cancel / retry 语义

| 动作 | 语义 |
|------|------|
| `pause` | 纯本地动作；abort reply，保留 partial 文件与当前已接收字节数 |
| `cancel` | 纯本地动作；abort reply，删除 partial 文件与本地任务记录；下载接口是无状态流式响应，无需服务端 cancel API |
| `retry` | 若 `supports_range=true`，优先走 `Range` 续传；若不支持或 partial 文件失效，则回退到 full download |

### 7.6 Owner / Visitor 下载特殊点

| 主题 | Owner 下载 | Visitor 下载 |
|------|------|------|
| 权限前置 | 文件归属由 `FileService::GetDownloadData` 校验 | `ShareService::GetDownloadInfo` 先校验分享权限 |
| `ShareAccessDenied` | 不适用 | `permission != download` 时返回 `403 60004`，浏览继续 |
| Range 支持 | `/info` 与下载数据都声明 `supports_range=true` | 实际下载由 `DownloadResponder` 统一支持 `Range` |

---

## 8. Retry 边界、取消边界与错误决策表

### 8.1 客户端 retry 边界（客户端策略，不代表服务端承诺）

| 条件 | 是否 retryable | 客户端策略 | 上限 |
|------|------|------|------|
| `40108 TokenExpired`（Owner） | 条件式是 | 触发 `single-flight` refresh，成功后 replay 原请求 | refresh 1 次 + replay 1 次 |
| `40108 TokenExpired`（Visitor） | 条件式是 | 重新 `POST /api/share/access/{share_id}` 获取新 share token | reverify 1 次；失败则停 |
| `40105 InvalidRefreshToken` | 否 | 清空 OwnerSession，跳登录页 | 0 |
| `40110 RefreshTokenAlreadyUsed` | 否 | 清空 OwnerSession；不再重试 refresh | 0 |
| `40111 TokenRevoked` | 否 | 清空对应会话 | 0 |
| `403 60004 ShareAccessDenied` | 否 | 保持 VisitorSession，禁用下载 | 0 |
| `404`（`FileNotFound` / `ShareNotFound` / `ShareExpired`） | 否 | 刷新列表或关闭分享，不自动 retry | 0 |
| `409`（`FileAlreadyExists` / `FolderAlreadyExists`） | 否 | 让用户改名或换目标 | 0 |
| `429 TooManyRequests` | 是 | 等待 `X-RateLimit-Reset` 后 retry | 最多 3 次 |
| `5xx` / `70001` / `70002` / `50011 FileReadError` | 是 | 指数退避 retry | 最多 3 次 |
| `network drop` / TLS 断开 / 连接超时 | 是（按端点） | 读请求、chunk、range 下载可 retry；未知副作用写请求不做全局重放 | 最多 3 次 |
| `50004 StorageQuotaExceeded` | 否 | 停止上传/复制，刷新配额展示 | 0 |
| `50008 UploadTaskNotFound` | 条件式是 | 重新 init，拿到新任务或续传信息 | 不重试旧 task，改走新 init 1 次 |

### 8.2 取消语义决策表

| 场景 | 客户端动作 | 结果态 |
|------|------|------|
| 用户取消上传 | 发 `DELETE /api/file/upload/{upload_id}`；在成功前保留 `CancelPending` | `Cancelled` |
| 上传 cancel 过程中 `network drop` | retry 同一个 cancel 请求；该请求对终态任务仍安全 | `Cancelled` 或 `Expired` |
| 用户 pause 下载 | 仅本地 abort，保留 partial 文件 | `Paused` |
| 用户 cancel 下载 | 本地 abort + 删除 partial 文件，不调用服务端 | `Cancelled` |
| 应用退出时有活跃下载 | 默认等价于 pause，而不是 cancel | `Paused` |

### 8.3 实现上必须显式处理的边缘条件

| 条件 | 必须行为 |
|------|------|
| 并发多个 Owner 请求同时收到 `401` | 只能有一个 refresh；其它请求等待同一个 `single-flight` 结果 |
| `network drop` 发生在 upload chunk | 允许 retry 同一 chunk；不得跳过该 chunk 直接 complete |
| `network drop` 发生在 complete upload | 可对同一 `upload_id` retry 一次；若结果仍不确定，刷新目录/重新 init 做对账 |
| 本地 partial 文件长度大于服务端文件大小 | 下一次下载必须触发 `416` 恢复路径，不能继续从错误偏移写入 |
| `TokenExpired` 与 `ShareAccessDenied` 同时被统一映射成“下载失败” | 禁止；两者动作完全不同 |

---

## 9. C++/Qt 实现建议

### 9.1 分层建议

| 层 | 建议职责 |
|------|------|
| `NetworkClient`（C++） | 发请求、取消请求、解析响应、归一化错误 |
| `RequestFactory`（C++） | 按 `auth_domain` 预配置头部；Owner 做 bearer injection，Visitor 注入 `X-Share-Token` |
| `SessionStore`（C++） | 管理 `OwnerSession` / `ShareVisitorSession` 状态机、`single-flight` refresh |
| `TransferStore`（C++） | 管理 `UploadTask` / `DownloadTask`，维护 `reserved` overlay 与 pause/retry/cancel |
| QML | 只绑定状态、只发意图信号，不直接操作 token 或 `QNetworkReply` |

### 9.2 结论

桌面端应采用 **一个 C++ 网络层 + 一个预配置请求工厂 + QML 只消费状态** 的模式：

1. 认证、刷新、重放、取消必须集中在 C++，否则 `single-flight` 无法成立。
2. 上传/下载状态必须由 C++ 任务模型持有，QML 只显示进度、失败原因和下一步动作。
3. `Authorization: Bearer` 与 `X-Share-Token` 的注入不能散落在页面组件里，必须由请求工厂按认证域统一完成。

---

## 10. 参考资料

| 文档 / 代码 | 路径 |
|------|------|
| 桌面端范围与成功标准 | `docs/desktop/00-scope-and-success.md` |
| 桌面端后端能力映射 | `docs/desktop/01-backend-capability-map.md` |
| 桌面端领域模型与错误分类 | `docs/desktop/02-domain-models-and-errors.md` |
| API 接口设计 | `docs/design/02-API接口设计.md` |
| 数据库设计 | `docs/design/03-数据库设计.md` |
| JWT 过滤器 | `src/filters/JwtAuthFilter.cpp` |
| 分享过滤器 | `src/filters/ShareAuthFilter.cpp` |
| 认证服务 | `src/services/AuthService.cpp` |
| 令牌服务 | `src/services/TokenService.cpp` |
| 文件服务 | `src/services/FileService.cpp` |
| 分享服务 | `src/services/ShareService.cpp` |
| 清理服务 | `src/services/CleanupService.cpp` |
| 下载响应器 | `src/controllers/DownloadResponder.cpp` |
| 运行时配置 | `config.json` |
