# Vue Web 客户端 - API 对接与类型定义

本文档为 Disk 网盘系统 Vue Web 客户端的前端调用规范，基于后端权威 API 设计文档和 DTO 定义编写。所有 TypeScript 接口可直接用于前端 Axios 封装和 Pinia Store 类型约束。

---

## 1. API 概述

### 1.1 基础信息

| 项目 | 说明 |
|------|------|
| Base URL | `/api` |
| 协议 | HTTPS |
| 数据格式 | JSON |
| 字符编码 | UTF-8 |
| 时间格式 | ISO 8601（如 `2026-01-13T10:30:00Z`） |

### 1.2 认证方式

前端需要支持三种认证域，通过不同的请求头传递：

| 认证方式 | Header | 适用场景 |
|----------|--------|----------|
| **Bearer** | `Authorization: Bearer <access_token>` | 已登录用户（Owner）访问个人资源 |
| **Share-Token** | `X-Share-Token: <share_token>` | 访客访问公开分享内容 |
| **Public** | 无需认证头 | 注册、登录、刷新令牌、健康检查、分享密码验证 |

### 1.3 统一响应格式

```typescript
interface ApiResponse<T> {
  code: number;      // 业务错误码，0 表示成功
  message: string;   // 提示消息
  data: T | null;    // 业务数据
}
```

### 1.4 统一分页格式

```typescript
interface Pagination {
  page: number;
  page_size: number;
  total: number;
  total_pages: number;
}

interface PaginatedResponse<T> {
  items: T[];
  pagination: Pagination;
}
```

---

## 2. 认证接口（Auth）

### POST /api/auth/register
**认证**: Public

**请求**:
```typescript
interface RegisterRequest {
  username: string;  // 4-32字符，字母数字下划线
  email: string;     // 有效邮箱格式
  password: string;  // 8-64字符，仅含大小写字母和数字，需同时包含大小写字母和数字
}
```

**响应**:
```typescript
interface RegisterResponse {
  id: number;
  username: string;
  email: string;
  nickname: string;
  storage_quota: number;  // 字节
  storage_used: number;   // 字节
  created_at: string;     // ISO 8601
}
```

**错误码**:
- `40001` - 用户名已存在
- `40002` - 邮箱已存在
- `10002` - 参数校验失败

---

### POST /api/auth/login
**认证**: Public

**请求**:
```typescript
interface LoginRequest {
  account: string;   // 用户名或邮箱
  password: string;  // 密码
}
```

**响应**:
```typescript
interface LoginResponse {
  access_token: string;
  refresh_token: string;
  token_type: string;   // "Bearer"
  expires_in: number;   // 秒，默认 7200
  user: {
    id: number;
    username: string;
    email: string;
    nickname: string;
    avatar: string | null;
    storage_used: number;
    storage_quota: number;
  };
}
```

**错误码**:
- `40101` - 用户名或密码错误
- `40102` - 账户已锁定
- `40103` - 账户已禁用

---

### POST /api/auth/refresh
**认证**: Public

**请求**:
```typescript
interface RefreshTokenRequest {
  refresh_token: string;
}
```

**响应**:
```typescript
interface RefreshTokenResponse {
  access_token: string;
  refresh_token: string;
  expires_in: number;  // 秒
}
```

**错误码**:
- `40105` - 刷新令牌无效
- `40110` - 刷新令牌已被使用

---

### POST /api/auth/logout
**认证**: Bearer

**请求**: 无请求体，通过 `Authorization: Bearer <token>` 认证

**响应**:
```typescript
// data 为 null
```

**错误码**:
- `40104` - 令牌无效或已过期
- `40111` - 令牌已被注销

---

## 3. 用户接口（User）

### GET /api/user/profile
**认证**: Bearer

**请求**: 无请求体

**响应**:
```typescript
interface UserProfileResponse {
  user: {
    id: number;
    username: string;
    email: string;
    nickname: string;
    avatar: string;
    storage_used: number;
    storage_quota: number;
    file_count: number;
    folder_count: number;
    created_at: string;
    updated_at: string;
  };
}
```

**错误码**:
- `40106` - 未提供令牌
- `40107` - 令牌格式错误
- `40108` - 令牌已过期

---

### PATCH /api/user/profile
**认证**: Bearer

**请求头**: 可选 `If-Match: <etag>`（乐观锁）

**请求**:
```typescript
interface UpdateProfileRequest {
  nickname?: string;  // 1-64字符，去除首尾空格
  avatar?: string;    // 1-512字符，必须是 https URL，禁止内网地址
}
```

**响应**:
```typescript
interface UpdateProfileResponse {
  user: UserProfileResponse['user'];
}
```

**错误码**:
- `10001` - 请求参数错误（空请求体、无可更新字段）
- `10002` - 参数校验失败（昵称长度、avatar 安全约束、显式 null）
- `10004` - 资源冲突（If-Match 不匹配）

---

### PUT /api/user/password
**认证**: Bearer

**请求**:
```typescript
interface ChangePasswordRequest {
  old_password: string;
  new_password: string;  // 8-64字符，需含大小写字母和数字，不能与旧密码相同
}
```

**响应**:
```typescript
// data 为 null
```

**错误码**:
- `40101` - 旧密码错误
- `10002` - 新密码格式错误或与旧密码相同

---

### GET /api/user/storage
**认证**: Bearer

**请求**: 无请求体

**响应**:
```typescript
interface StorageResponse {
  used: number;        // 实际已使用（含回收站）
  reserved: number;    // 已预留但未完成的上传空间
  quota: number;       // 总配额
  percentage: number;  // 使用百分比，1位小数
  categories: {
    type: string;      // document/image/video/audio/other
    size: number;
    count: number;
  }[];
}
```

**错误码**:
- `40106` - 未提供令牌
- `40108` - 令牌已过期

---

## 4. 文件接口（File）

### POST /api/file/upload/init
**认证**: Bearer（受上传限流保护）

**请求**:
```typescript
interface InitUploadRequest {
  filename: string;    // 1-255字符，禁止 / \ : * ? " < > | 及控制字符，禁止 . 和 .. 开头
  file_size: number;   // 字节，必须 > 0
  file_hash: string;   // 32字符小写十六进制 MD5
  parent_id?: number;  // 默认 0（根目录）
}
```

**响应**:
```typescript
interface InitUploadResponse {
  upload_id: string;           // 上传会话 ID（秒传时为空）
  chunk_size: number;          // 分片大小（字节）
  total_chunks: number;        // 总分片数
  uploaded_chunks: number[];   // 已上传的分片索引（断点续传）
  instant_upload: boolean;     // 是否秒传
  file?: {                     // 秒传时返回
    id: number;
    name: string;
    size: number;
    hash: string;
    mime_type: string;
    parent_id: number;
    created_at: string;
  };
}
```

**错误码**:
- `10001` - 请求参数错误
- `10002` - 参数校验失败
- `50004` - 存储空间不足（预占用失败）
- `50006` - 文件夹不存在

---

### POST /api/file/upload/chunk
**认证**: Bearer（受上传限流保护）

**请求头**: `Content-Type: application/octet-stream`

**查询参数**:
```typescript
interface UploadChunkQuery {
  upload_id: string;    // 上传会话 ID
  chunk_index: number;  // 分片索引（从 0 开始）
  chunk_hash: string;   // 分片 MD5 哈希（32字符小写十六进制）
}
```

**请求体**: 二进制数据（原始分片内容）

**响应**:
```typescript
interface UploadChunkResponse {
  chunk_index: number;
  uploaded: boolean;
}
```

**错误码**:
- `10001` - 请求参数错误
- `10002` - 参数校验失败（chunk_hash 格式错误）
- `50008` - 上传任务不存在或已过期
- `50009` - 分片校验失败

---

### POST /api/file/upload/complete
**认证**: Bearer（受上传限流保护）

**请求**:
```typescript
interface CompleteUploadRequest {
  upload_id: string;
}
```

**响应**:
```typescript
interface CompleteUploadResponse {
  file: {
    id: number;
    name: string;
    size: number;
    hash: string;
    mime_type: string;
    parent_id: number;
    created_at: string;
  };
}
```

**错误码**:
- `10001` - 请求参数错误
- `10002` - 参数校验失败（分片不完整、哈希校验失败）
- `50008` - 上传任务不存在或已过期

---

### DELETE /api/file/upload/{upload_id}
**认证**: Bearer（受上传限流保护）

**路径参数**: `upload_id: string`

**响应**:
```typescript
// data 为 null
```

**错误码**:
- `10001` - 请求参数错误
- `50008` - 上传任务不存在或已过期

---

### GET /api/file/download/{file_id}/info
**认证**: Bearer

**路径参数**: `file_id: number`

**响应**:
```typescript
interface DownloadInfoResponse {
  file_id: number;
  filename: string;
  file_size: number;
  file_hash: string;
  mime_type: string;
  supports_range: boolean;
}
```

**错误码**:
- `10001` - 请求参数错误
- `50005` - 文件不存在

---

### GET /api/file/download/{file_id}
**认证**: Bearer

**路径参数**: `file_id: number`

**请求头**: 可选 `Range: bytes=<start>-<end>`

**响应**: 文件二进制数据流

**响应头**:
- `Content-Type`: 文件 MIME 类型
- `Content-Length`: 返回数据长度
- `Content-Disposition`: `attachment; filename="..."`
- `Accept-Ranges`: `bytes`
- `Content-Range`: 范围信息（Range 请求时）

**HTTP 状态码**:
- `200` - 完整下载
- `206` - 范围下载成功
- `416` - 范围无效

**错误码**:
- `10001` - 请求参数错误
- `10002` - 请求范围无效
- `50005` - 文件不存在

---

### GET /api/file/list
**认证**: Bearer

**查询参数**:
```typescript
interface FileListQuery {
  parent_id?: number;   // 默认 0
  page?: number;        // 默认 1
  page_size?: number;   // 默认 20，最大 100
  sort_by?: string;     // name/size/created_at/updated_at
  sort_order?: string;  // asc/desc，默认 asc
  type?: string;        // all/file/folder，默认 all
}
```

**响应**:
```typescript
interface FileListItem {
  id: number;
  name: string;
  type: 'file' | 'folder';
  // 文件特有
  size?: number;
  mime_type?: string;
  hash?: string;
  // 文件夹特有
  item_count?: number;
  created_at: string;
  updated_at: string;
}

interface FileListResponse {
  items: FileListItem[];
  pagination: Pagination;
}
```

**错误码**:
- `10001` - 请求参数错误
- `10002` - 参数校验失败
- `50006` - 文件夹不存在

---

### GET /api/file/search
**认证**: Bearer

**查询参数**:
```typescript
interface SearchQuery {
  keyword: string;      // 1-100字符，必填
  type?: string;        // all/file/folder，默认 all
  folder_id?: number;   // 限定搜索范围
  page?: number;        // 默认 1
  page_size?: number;   // 默认 20，最大 100
}
```

**响应**:
```typescript
interface SearchResultItem {
  id: number;
  name: string;
  type: 'file' | 'folder';
  size?: number;
  mime_type?: string;
  hash?: string;
  item_count?: number;
  path: string;         // 面包屑路径
  created_at: string;
  updated_at: string;
}

interface SearchResponse {
  items: SearchResultItem[];
  pagination: Pagination;
}
```

**错误码**:
- `10001` - 请求参数错误
- `10002` - 参数校验失败（keyword 为空、长度超限、包含禁止字符）

---

### GET /api/file/{file_id}
**认证**: Bearer

**路径参数**: `file_id: number`

**响应**:
```typescript
interface FileDetailResponse {
  id: number;
  name: string;
  type: 'file' | 'folder';
  size: number;
  hash: string;
  mime_type: string;
  parent_id: number;
  path: string;
  created_at: string;
  updated_at: string;
}
```

**错误码**:
- `10001` - 请求参数错误
- `50005` - 文件不存在

---

### PUT /api/file/{file_id}/rename
**认证**: Bearer

**路径参数**: `file_id: number`（可以是文件 ID 或文件夹 ID）

**请求**:
```typescript
interface RenameRequest {
  new_name: string;  // 1-255字符，同文件名约束
}
```

**响应**:
```typescript
interface RenameResponse {
  id: number;
  name: string;
  updated_at: string;
}
```

**错误码**:
- `10001` - 请求参数错误
- `10002` - 参数校验失败
- `50001` - 文件名无效
- `50005` - 文件不存在
- `50007` - 文件已存在（同目录下同名）

---

### PUT /api/file/move
**认证**: Bearer

**请求**:
```typescript
interface MoveRequest {
  file_ids?: number[];     // 文件 ID 列表
  folder_ids?: number[];   // 文件夹 ID 列表
  target_folder_id: number; // 目标文件夹 ID，默认 0
}
```

**响应**:
```typescript
interface MoveResponse {
  moved_count: number;
  moved_file_count: number;
  moved_folder_count: number;
}
```

**错误码**:
- `10001` - 请求参数错误
- `10002` - 参数校验失败
- `50005` - 文件不存在
- `50006` - 文件夹不存在（目标）

---

### POST /api/file/copy
**认证**: Bearer

**请求**:
```typescript
interface CopyRequest {
  file_ids?: number[];
  folder_ids?: number[];
  target_folder_id: number;
}
```

**响应**:
```typescript
interface CopyResponse {
  copied_count: number;
  new_files: {
    old_id: number;
    new_id: number;
  }[];
}
```

**错误码**:
- `10001` - 请求参数错误
- `50004` - 存储空间不足
- `50005` - 文件不存在
- `50006` - 文件夹不存在

---

### DELETE /api/file
**认证**: Bearer

**请求**:
```typescript
interface DeleteRequest {
  file_ids?: number[];
  folder_ids?: number[];
}
```

**响应**:
```typescript
interface DeleteResponse {
  deleted_count: number;
}
```

**错误码**:
- `10001` - 请求参数错误
- `50005` - 文件不存在

---

## 5. 目录接口（Folder）

### POST /api/folder/create
**认证**: Bearer

**请求**:
```typescript
interface CreateFolderRequest {
  name: string;       // 1-255字符，同文件名约束
  parent_id?: number; // 默认 0
}
```

**响应**:
```typescript
interface CreateFolderResponse {
  id: number;
  name: string;
  parent_id: number;
  path: string;
  created_at: string;
}
```

**错误码**:
- `10002` - 参数校验失败（名称长度）
- `50001` - 文件名无效（禁止字符、保留名称等）
- `50006` - 父文件夹不存在
- `50010` - 同名文件夹已存在

---

### GET /api/folder/tree
**认证**: Bearer

**查询参数**:
```typescript
interface FolderTreeQuery {
  parent_id?: number; // 默认 0
  depth?: number;     // 默认 -1（全部）
}
```

**响应**:
```typescript
interface FolderTreeNode {
  id: number;
  name: string;
  children: FolderTreeNode[];
}

interface FolderTreeResponse {
  id: number;
  name: string;
  children: FolderTreeNode[];
}
```

**错误码**:
- `10002` - 参数校验失败

---

### GET /api/folder/{folder_id}/breadcrumb
**认证**: Bearer

**路径参数**: `folder_id: number`

**响应**:
```typescript
interface BreadcrumbItem {
  id: number;
  name: string;
}

interface BreadcrumbResponse {
  path: BreadcrumbItem[];
}
```

**错误码**:
- `10001` - 请求参数错误
- `50006` - 文件夹不存在

---

### PUT /api/folder/{folder_id}/rename
**认证**: Bearer

**请求**:
```typescript
interface RenameRequest {
  new_name: string;
}
```

**响应**:
```typescript
interface RenameFolderResponse {
  id: number;
  name: string;
  path: string;
  updated_at: string;
}
```

**错误码**:
- `10001` - 请求参数错误
- `10002` - 参数校验失败
- `50001` - 文件夹名称无效
- `50006` - 文件夹不存在
- `50010` - 同名文件夹已存在

---

## 6. 回收站接口（Trash）

### GET /api/trash
**认证**: Bearer（受限流保护）

**查询参数**:
```typescript
interface TrashListQuery {
  page?: number;      // 默认 1
  page_size?: number; // 默认 20
}
```

**响应**:
```typescript
interface TrashItem {
  id: number;
  original_id: number;
  name: string;
  type: 'file' | 'folder';
  size: number;
  original_path: string;
  deleted_at: string;
  expires_at: string;
}

interface TrashListResponse {
  items: TrashItem[];
  pagination: Pagination;
}
```

**错误码**:
- `10001` - 请求参数错误

---

### POST /api/trash/restore
**认证**: Bearer（受限流保护）

**请求**:
```typescript
interface TrashRestoreRequest {
  trash_ids: number[];  // 1-100 项
}
```

**响应**:
```typescript
interface RestoreResultItem {
  trash_id: number;
  status: 'success' | 'failed';
  file_id?: number;
  folder_id?: number;
  path?: string;
  error?: {
    code: number;
    message: string;
    field?: string;
    value?: string;
  };
}

interface TrashRestoreResponse {
  summary: {
    total: number;
    success_count: number;
    failure_count: number;
  };
  results: RestoreResultItem[];
}
```

**错误码**:
- `10001` - 请求参数错误
- `10003` - 资源不存在

---

### DELETE /api/trash
**认证**: Bearer（受限流保护）

**请求**:
```typescript
interface TrashDeleteRequest {
  trash_ids: number[];
}
```

**响应**:
```typescript
interface DeleteResultItem {
  trash_id: number;
  status: 'success' | 'failed';
  freed_space?: number;
  error?: {
    code: number;
    message: string;
  };
}

interface TrashDeleteResponse {
  summary: {
    total: number;
    success_count: number;
    failure_count: number;
  };
  results: DeleteResultItem[];
}
```

**错误码**:
- `10001` - 请求参数错误
- `10003` - 资源不存在

---

### DELETE /api/trash/all
**认证**: Bearer（受限流保护）

**请求**: 无请求体

**响应**:
```typescript
interface TrashDeleteAllResponse {
  deleted_count: number;
  freed_space: number;
}
```

**错误码**:
- `40106` - 未提供令牌

---

## 7. 分享接口（Share）

### POST /api/share
**认证**: Bearer

**请求**:
```typescript
interface CreateShareRequest {
  file_ids?: number[];      // 至少提供 file_ids 或 folder_ids 之一
  folder_ids?: number[];
  expire_days?: number;     // 默认 7，0 表示永久
  password?: string;        // 可选，4-8 字符
  permission?: 'view' | 'download'; // 默认 download
}
```

**响应**:
```typescript
interface CreateShareResponse {
  share_id: string;
  share_link: string;
  password?: string;
  permission: string;
  expires_at: string;
  created_at: string;
}
```

**错误码**:
- `10001` - 请求参数错误（file_ids 为空）
- `10002` - 参数校验失败（password 长度）
- `50005` - 文件不存在或不属于当前用户

---

### GET /api/share
**认证**: Bearer

**查询参数**:
```typescript
interface ShareListQuery {
  status?: 'all' | 'active' | 'expired' | 'cancelled'; // 默认 all
  page?: number;
  page_size?: number;
}
```

**响应**:
```typescript
interface ShareItem {
  share_id: string;
  file_name: string;
  file_count: number;
  share_link: string;
  has_password: boolean;
  permission: string;
  view_count: number;
  download_count: number;
  created_at: string;
  expires_at: string;
  status: 'active' | 'expired' | 'cancelled';
}

interface ShareListResponse {
  items: ShareItem[];
  pagination: Pagination;
}
```

**错误码**:
- `10001` - 请求参数错误

---

### GET /api/share/{share_id}
**认证**: Bearer

**路径参数**: `share_id: string`

**响应**:
```typescript
interface ShareFile {
  id: number;
  name: string;
  type: 'file' | 'folder';
  size: number;
  item_count?: number;  // 文件夹时有
}

interface ShareDetailResponse {
  share_id: string;
  files: ShareFile[];
  share_link: string;
  has_password: boolean;
  permission: string;
  view_count: number;
  download_count: number;
  created_at: string;
  expires_at: string;
  status: string;
}
```

**错误码**:
- `10001` - 请求参数错误
- `60001` - 分享不存在

---

### PUT /api/share/{share_id}
**认证**: Bearer

**路径参数**: `share_id: string`

**请求**:
```typescript
interface UpdateShareRequest {
  expire_days?: number;     // 从当前时间计算的新有效期
  password?: string;        // 新密码，空字符串表示移除密码
  permission?: 'view' | 'download';
}
```

**响应**:
```typescript
interface UpdateShareResponse {
  share_id: string;
  expires_at: string;
  has_password: boolean;
  permission: string;
  updated_at: string;
}
```

**错误码**:
- `10001` - 请求参数错误
- `10002` - 参数校验失败
- `60001` - 分享不存在

---

### DELETE /api/share
**认证**: Bearer

**请求**:
```typescript
interface CancelShareRequest {
  share_ids: string[];
}
```

**响应**:
```typescript
interface CancelShareResult {
  share_id: string;
  status: 'success' | 'failed';
  error?: {
    code: number;
    message: string;
    reason: string;
  };
}

interface CancelShareResponse {
  summary: {
    total: number;
    succeeded: number;
    failed: number;
  };
  results: CancelShareResult[];
}
```

**错误码**:
- `10001` - 请求参数错误

---

### POST /api/share/access/{share_id}
**认证**: Public

**路径参数**: `share_id: string`

**请求**:
```typescript
interface AccessShareRequest {
  password?: string;  // 分享设置了密码时必填
}
```

**响应**:
```typescript
interface AccessShareResponse {
  share_token: string;
  expires_in: number;   // 秒，默认 3600
  permission: string;
  files: ShareFile[];
}
```

**错误码**:
- `10001` - 请求参数错误
- `60001` - 分享不存在
- `60002` - 分享已过期
- `60003` - 分享密码错误（含锁定场景）

---

### GET /api/share/browse/{share_id}
**认证**: Share-Token

**路径参数**: `share_id: string`

**查询参数**:
```typescript
interface BrowseShareQuery {
  folder_id?: number;  // 分享内的相对文件夹 ID
}
```

**响应**:
```typescript
interface BrowseItem {
  id: number;
  name: string;
  type: 'file' | 'folder';
  size: number;
  item_count?: number;
}

interface BrowseBreadcrumb {
  id: number;
  name: string;
}

interface BrowseShareResponse {
  items: BrowseItem[];
  breadcrumb: BrowseBreadcrumb[];
}
```

**错误码**:
- `10001` - 请求参数错误
- `40106` - 未提供 Share-Token
- `40108` - Share-Token 已过期
- `60001` - 分享不存在
- `60002` - 分享已过期
- `50006` - 文件夹不存在于分享中

---

### GET /api/share/download/{share_id}/{file_id}
**认证**: Share-Token

**路径参数**:
- `share_id: string`
- `file_id: number`

**请求头**: 可选 `Range: bytes=<start>-<end>`

**响应**: 文件二进制数据流

**HTTP 状态码**:
- `200` - 完整下载
- `206` - 范围下载
- `416` - 范围无效

**错误码**:
- `10001` - 请求参数错误
- `60001` - 分享不存在
- `60002` - 分享已过期
- `60004` - 无权限访问（仅查看分享不允许下载）
- `50005` - 文件不存在于分享中

---

## 8. 系统与日志接口（System / Logs）

### GET /api/health
**认证**: Public

**请求**: 无

**响应**:
```typescript
interface HealthResponse {
  overall_status: 'healthy' | 'degraded' | 'unhealthy';
  version: string;
  uptime: number;       // 秒
  timestamp: string;
  components: {
    database: {
      status: 'healthy' | 'unhealthy';
      latency_ms?: number;
      message?: string;
    };
    redis: {
      status: 'healthy' | 'unhealthy';
      latency_ms?: number;
      message?: string;
    };
  };
}
```

**HTTP 状态码**:
- `200` - 系统健康
- `503` - 系统不健康或降级

---

### GET /api/system/info
**认证**: Bearer

**请求**: 无

**响应**:
```typescript
interface SystemInfoResponse {
  version: string;
  drogon_version: string;
  build_time: string;
  uptime: number;
  connections: {
    current: number;
    peak: number;
  };
  storage: {
    total_users: number;
    total_files: number;
    total_folders: number;
    total_size: number;
  };
}
```

**错误码**:
- `40106` - 未提供令牌
- `40108` - 令牌已过期

---

### GET /api/logs
**认证**: Bearer

**查询参数**:
```typescript
interface LogsQuery {
  page?: number;      // 默认 1
  page_size?: number; // 默认 20，最大 100
}
```

**响应**:
```typescript
interface LogItem {
  id: number;
  action: 'login' | 'logout' | 'upload' | 'download' | 'delete' | 'rename' | 'move' | 'copy' | 'share' | 'restore';
  target_type: 'file' | 'folder' | 'share' | 'user';
  target_id?: number;
  target_name?: string;
  details?: string;     // JSON 字符串
  ip_address: string;
  created_at: string;
}

interface LogsResponse {
  items: LogItem[];
  total: number;
  page: number;
  page_size: number;
}
```

**错误码**:
- `10001` - 请求参数错误
- `40106` - 未提供令牌

---

## 9. 管理员接口（Admin）

所有管理员接口需要 JWT 角色为管理员（role=1），由 AdminAuthFilter 校验。

### GET /api/admin/users
**认证**: Bearer + Admin

**查询参数**:
```typescript
interface AdminListUsersQuery {
  username?: string;   // 模糊匹配
  email?: string;      // 模糊匹配
  status?: number;     // 0/1/2
  role?: number;       // 0/1
  page?: number;       // 默认 1
  page_size?: number;  // 默认 20，最大 100
}
```

**响应**:
```typescript
interface AdminUserItem {
  id: number;
  username: string;
  email: string;
  nickname: string;
  status: number;
  role: number;
  storage_used: number;
  storage_quota: number;
  file_count: number;
  folder_count: number;
  created_at: string;
  updated_at: string;
}

interface AdminUserListResponse {
  items: AdminUserItem[];
  pagination: Pagination;
}
```

**错误码**:
- `80001` - 需要管理员权限

---

### GET /api/admin/users/{id}
**认证**: Bearer + Admin

**路径参数**: `id: number`

**响应**:
```typescript
interface AdminUserDetailResponse {
  id: number;
  username: string;
  email: string;
  nickname: string;
  avatar: string;
  status: number;
  role: number;
  storage_used: number;
  storage_quota: number;
  file_count: number;
  folder_count: number;
  created_at: string;
  updated_at: string;
}
```

**错误码**:
- `80001` - 需要管理员权限
- `80002` - 用户不存在

---

### PUT /api/admin/users/{id}/status
**认证**: Bearer + Admin

**路径参数**: `id: number`

**请求**:
```typescript
interface AdminChangeStatusRequest {
  status: number;  // 0（禁用）/1（正常）/2（锁定）
}
```

**响应**:
```typescript
interface AdminChangeStatusResponse {
  id: number;
  status: number;
  updated_at: string;
}
```

**错误码**:
- `80001` - 需要管理员权限
- `80002` - 用户不存在
- `80003` - 不能修改自己的状态
- `80004` - 不能降级最后一个管理员
- `80006` - 无效的用户状态

---

### PUT /api/admin/users/{id}/role
**认证**: Bearer + Admin

**路径参数**: `id: number`

**请求**:
```typescript
interface AdminChangeRoleRequest {
  role: number;  // 0（普通用户）/1（管理员）
}
```

**响应**:
```typescript
interface AdminChangeRoleResponse {
  id: number;
  role: number;
  updated_at: string;
}
```

**错误码**:
- `80001` - 需要管理员权限
- `80002` - 用户不存在
- `80003` - 不能修改自己的角色
- `80004` - 不能降级最后一个管理员
- `80007` - 无效的角色

---

### DELETE /api/admin/users/{id}
**认证**: Bearer + Admin

**路径参数**: `id: number`

**响应**:
```typescript
interface AdminDeleteUserResponse {
  id: number;
  status: number;
  updated_at: string;
}
```

**错误码**:
- `80001` - 需要管理员权限
- `80002` - 用户不存在
- `80003` - 不能删除自己
- `80004` - 不能删除最后一个管理员

---

### GET /api/admin/storage/stats
**认证**: Bearer + Admin

**响应**:
```typescript
interface AdminStorageStatsResponse {
  total_users: number;
  total_files: number;
  total_folders: number;
  total_size: number;
  user_count: number;
  active_user_count: number;
}
```

**错误码**:
- `80001` - 需要管理员权限

---

### GET /api/admin/shares
**认证**: Bearer + Admin

**查询参数**:
```typescript
interface AdminListSharesQuery {
  status?: number;      // 0/1/2
  user_id?: number;
  username?: string;    // 模糊匹配
  page?: number;
  page_size?: number;
}
```

**响应**:
```typescript
interface AdminShareItem {
  share_id: string;
  user_id: number;
  username: string;
  file_name: string;
  file_count: number;
  share_link: string;
  has_password: boolean;
  permission: string;
  view_count: number;
  download_count: number;
  created_at: string;
  expires_at: string;
  status: number;
}

interface AdminShareListResponse {
  items: AdminShareItem[];
  pagination: Pagination;
}
```

**错误码**:
- `80001` - 需要管理员权限

---

### GET /api/admin/shares/{id}
**认证**: Bearer + Admin

**路径参数**: `id: string`（share_code）

**响应**:
```typescript
interface AdminShareDetailResponse {
  share_id: string;
  user_id: number;
  username: string;
  files: ShareFile[];
  share_link: string;
  has_password: boolean;
  permission: string;
  view_count: number;
  download_count: number;
  created_at: string;
  expires_at: string;
  status: string;
}
```

**错误码**:
- `80001` - 需要管理员权限
- `80005` - 分享不存在

---

### DELETE /api/admin/shares/{id}
**认证**: Bearer + Admin

**路径参数**: `id: string`（share_code）

**响应**:
```typescript
interface AdminDeleteShareResponse {
  share_id: string;
  status: string;
  updated_at: string;
}
```

**错误码**:
- `80001` - 需要管理员权限
- `80005` - 分享不存在

---

### GET /api/admin/stats/overview
**认证**: Bearer + Admin

**响应**:
```typescript
interface AdminStatsOverviewResponse {
  user_count: number;
  file_count: number;
  storage_size: number;
  share_count: number;
  active_share_count: number;
  storage_quota: number;
}
```

**错误码**:
- `80001` - 需要管理员权限

---

### GET /api/admin/stats/system
**认证**: Bearer + Admin

**响应**:
```typescript
interface AdminStatsSystemResponse {
  uptime: number;
  version: string;
  mysql: {
    connected: boolean;
    connection_count: number;
    latency_ms: number;
  };
  redis: {
    connected: boolean;
    latency_ms: number;
  };
  disk: {
    total: number;
    used: number;
    free: number;
    percentage: number;
  };
}
```

**错误码**:
- `80001` - 需要管理员权限

---

## 10. TypeScript 类型定义汇总

以下类型定义可直接放入前端项目的 `src/types/api.ts` 中，作为全局 API 类型约束。

```typescript
// ==================== 通用结构 ====================

export interface ApiResponse<T> {
  code: number;
  message: string;
  data: T | null;
}

export interface Pagination {
  page: number;
  page_size: number;
  total: number;
  total_pages: number;
}

export interface PaginatedData<T> {
  items: T[];
  pagination: Pagination;
}

// ==================== 认证 ====================

export interface RegisterRequest {
  username: string;
  email: string;
  password: string;
}

export interface RegisterResponse {
  id: number;
  username: string;
  email: string;
  nickname: string;
  storage_quota: number;
  storage_used: number;
  created_at: string;
}

export interface LoginRequest {
  account: string;
  password: string;
}

export interface LoginResponse {
  access_token: string;
  refresh_token: string;
  token_type: string;
  expires_in: number;
  user: {
    id: number;
    username: string;
    email: string;
    nickname: string;
    avatar: string | null;
    storage_used: number;
    storage_quota: number;
  };
}

export interface RefreshTokenRequest {
  refresh_token: string;
}

export interface RefreshTokenResponse {
  access_token: string;
  refresh_token: string;
  expires_in: number;
}

// ==================== 用户 ====================

export interface UserProfile {
  id: number;
  username: string;
  email: string;
  nickname: string;
  avatar: string;
  storage_used: number;
  storage_quota: number;
  file_count: number;
  folder_count: number;
  created_at: string;
  updated_at: string;
}

export interface UpdateProfileRequest {
  nickname?: string;
  avatar?: string;
}

export interface ChangePasswordRequest {
  old_password: string;
  new_password: string;
}

export interface StorageResponse {
  used: number;
  reserved: number;
  quota: number;
  percentage: number;
  categories: {
    type: string;
    size: number;
    count: number;
  }[];
}

// ==================== 文件 ====================

export interface FileItem {
  id: number;
  name: string;
  type: 'file' | 'folder';
  size?: number;
  mime_type?: string;
  hash?: string;
  item_count?: number;
  created_at: string;
  updated_at: string;
}

export interface InitUploadRequest {
  filename: string;
  file_size: number;
  file_hash: string;
  parent_id?: number;
}

export interface InitUploadResponse {
  upload_id: string;
  chunk_size: number;
  total_chunks: number;
  uploaded_chunks: number[];
  instant_upload: boolean;
  file?: {
    id: number;
    name: string;
    size: number;
    hash: string;
    mime_type: string;
    parent_id: number;
    created_at: string;
  };
}

export interface UploadChunkQuery {
  upload_id: string;
  chunk_index: number;
  chunk_hash: string;
}

export interface UploadChunkResponse {
  chunk_index: number;
  uploaded: boolean;
}

export interface CompleteUploadRequest {
  upload_id: string;
}

export interface CompleteUploadResponse {
  file: {
    id: number;
    name: string;
    size: number;
    hash: string;
    mime_type: string;
    parent_id: number;
    created_at: string;
  };
}

export interface DownloadInfoResponse {
  file_id: number;
  filename: string;
  file_size: number;
  file_hash: string;
  mime_type: string;
  supports_range: boolean;
}

export interface FileListQuery {
  parent_id?: number;
  page?: number;
  page_size?: number;
  sort_by?: string;
  sort_order?: string;
  type?: string;
}

export interface FileListResponse extends PaginatedData<FileItem> {}

export interface SearchQuery {
  keyword: string;
  type?: string;
  folder_id?: number;
  page?: number;
  page_size?: number;
}

export interface SearchResultItem extends FileItem {
  path: string;
}

export interface SearchResponse extends PaginatedData<SearchResultItem> {}

export interface FileDetailResponse {
  id: number;
  name: string;
  type: 'file' | 'folder';
  size: number;
  hash: string;
  mime_type: string;
  parent_id: number;
  path: string;
  created_at: string;
  updated_at: string;
}

export interface RenameRequest {
  new_name: string;
}

export interface RenameResponse {
  id: number;
  name: string;
  updated_at: string;
}

export interface MoveRequest {
  file_ids?: number[];
  folder_ids?: number[];
  target_folder_id: number;
}

export interface MoveResponse {
  moved_count: number;
  moved_file_count: number;
  moved_folder_count: number;
}

export interface CopyRequest {
  file_ids?: number[];
  folder_ids?: number[];
  target_folder_id: number;
}

export interface CopyResponse {
  copied_count: number;
  new_files: { old_id: number; new_id: number }[];
}

export interface DeleteRequest {
  file_ids?: number[];
  folder_ids?: number[];
}

export interface DeleteResponse {
  deleted_count: number;
}

// ==================== 目录 ====================

export interface CreateFolderRequest {
  name: string;
  parent_id?: number;
}

export interface CreateFolderResponse {
  id: number;
  name: string;
  parent_id: number;
  path: string;
  created_at: string;
}

export interface FolderTreeNode {
  id: number;
  name: string;
  children: FolderTreeNode[];
}

export interface FolderTreeResponse {
  id: number;
  name: string;
  children: FolderTreeNode[];
}

export interface BreadcrumbItem {
  id: number;
  name: string;
}

export interface BreadcrumbResponse {
  path: BreadcrumbItem[];
}

// ==================== 回收站 ====================

export interface TrashItem {
  id: number;
  original_id: number;
  name: string;
  type: 'file' | 'folder';
  size: number;
  original_path: string;
  deleted_at: string;
  expires_at: string;
}

export interface TrashListResponse extends PaginatedData<TrashItem> {}

export interface TrashRestoreRequest {
  trash_ids: number[];
}

export interface RestoreResultItem {
  trash_id: number;
  status: 'success' | 'failed';
  file_id?: number;
  folder_id?: number;
  path?: string;
  error?: {
    code: number;
    message: string;
    field?: string;
    value?: string;
  };
}

export interface TrashRestoreResponse {
  summary: {
    total: number;
    success_count: number;
    failure_count: number;
  };
  results: RestoreResultItem[];
}

export interface TrashDeleteRequest {
  trash_ids: number[];
}

export interface DeleteResultItem {
  trash_id: number;
  status: 'success' | 'failed';
  freed_space?: number;
  error?: {
    code: number;
    message: string;
  };
}

export interface TrashDeleteResponse {
  summary: {
    total: number;
    success_count: number;
    failure_count: number;
  };
  results: DeleteResultItem[];
}

export interface TrashDeleteAllResponse {
  deleted_count: number;
  freed_space: number;
}

// ==================== 分享 ====================

export interface CreateShareRequest {
  file_ids?: number[];
  folder_ids?: number[];
  expire_days?: number;
  password?: string;
  permission?: 'view' | 'download';
}

export interface CreateShareResponse {
  share_id: string;
  share_link: string;
  password?: string;
  permission: string;
  expires_at: string;
  created_at: string;
}

export interface ShareItem {
  share_id: string;
  file_name: string;
  file_count: number;
  share_link: string;
  has_password: boolean;
  permission: string;
  view_count: number;
  download_count: number;
  created_at: string;
  expires_at: string;
  status: 'active' | 'expired' | 'cancelled';
}

export interface ShareListResponse extends PaginatedData<ShareItem> {}

export interface ShareFile {
  id: number;
  name: string;
  type: 'file' | 'folder';
  size: number;
  item_count?: number;
}

export interface ShareDetailResponse {
  share_id: string;
  files: ShareFile[];
  share_link: string;
  has_password: boolean;
  permission: string;
  view_count: number;
  download_count: number;
  created_at: string;
  expires_at: string;
  status: string;
}

export interface UpdateShareRequest {
  expire_days?: number;
  password?: string;
  permission?: 'view' | 'download';
}

export interface UpdateShareResponse {
  share_id: string;
  expires_at: string;
  has_password: boolean;
  permission: string;
  updated_at: string;
}

export interface CancelShareRequest {
  share_ids: string[];
}

export interface CancelShareResult {
  share_id: string;
  status: 'success' | 'failed';
  error?: {
    code: number;
    message: string;
    reason: string;
  };
}

export interface CancelShareResponse {
  summary: {
    total: number;
    succeeded: number;
    failed: number;
  };
  results: CancelShareResult[];
}

export interface AccessShareRequest {
  password?: string;
}

export interface AccessShareResponse {
  share_token: string;
  expires_in: number;
  permission: string;
  files: ShareFile[];
}

export interface BrowseItem {
  id: number;
  name: string;
  type: 'file' | 'folder';
  size: number;
  item_count?: number;
}

export interface BrowseBreadcrumb {
  id: number;
  name: string;
}

export interface BrowseShareResponse {
  items: BrowseItem[];
  breadcrumb: BrowseBreadcrumb[];
}

// ==================== 系统与日志 ====================

export interface HealthResponse {
  overall_status: 'healthy' | 'degraded' | 'unhealthy';
  version: string;
  uptime: number;
  timestamp: string;
  components: {
    database: {
      status: 'healthy' | 'unhealthy';
      latency_ms?: number;
      message?: string;
    };
    redis: {
      status: 'healthy' | 'unhealthy';
      latency_ms?: number;
      message?: string;
    };
  };
}

export interface SystemInfoResponse {
  version: string;
  drogon_version: string;
  build_time: string;
  uptime: number;
  connections: {
    current: number;
    peak: number;
  };
  storage: {
    total_users: number;
    total_files: number;
    total_folders: number;
    total_size: number;
  };
}

export interface LogItem {
  id: number;
  action: string;
  target_type: string;
  target_id?: number;
  target_name?: string;
  details?: string;
  ip_address: string;
  created_at: string;
}

export interface LogsResponse {
  items: LogItem[];
  total: number;
  page: number;
  page_size: number;
}

// ==================== 管理员 ====================

export interface AdminUserItem {
  id: number;
  username: string;
  email: string;
  nickname: string;
  status: number;
  role: number;
  storage_used: number;
  storage_quota: number;
  file_count: number;
  folder_count: number;
  created_at: string;
  updated_at: string;
}

export interface AdminUserListResponse extends PaginatedData<AdminUserItem> {}

export interface AdminUserDetailResponse {
  id: number;
  username: string;
  email: string;
  nickname: string;
  avatar: string;
  status: number;
  role: number;
  storage_used: number;
  storage_quota: number;
  file_count: number;
  folder_count: number;
  created_at: string;
  updated_at: string;
}

export interface AdminChangeStatusRequest {
  status: number;
}

export interface AdminChangeRoleRequest {
  role: number;
}

export interface AdminDeleteUserResponse {
  id: number;
  status: number;
  updated_at: string;
}

export interface AdminStorageStatsResponse {
  total_users: number;
  total_files: number;
  total_folders: number;
  total_size: number;
  user_count: number;
  active_user_count: number;
}

export interface AdminShareItem {
  share_id: string;
  user_id: number;
  username: string;
  file_name: string;
  file_count: number;
  share_link: string;
  has_password: boolean;
  permission: string;
  view_count: number;
  download_count: number;
  created_at: string;
  expires_at: string;
  status: number;
}

export interface AdminShareListResponse extends PaginatedData<AdminShareItem> {}

export interface AdminShareDetailResponse {
  share_id: string;
  user_id: number;
  username: string;
  files: ShareFile[];
  share_link: string;
  has_password: boolean;
  permission: string;
  view_count: number;
  download_count: number;
  created_at: string;
  expires_at: string;
  status: string;
}

export interface AdminDeleteShareResponse {
  share_id: string;
  status: string;
  updated_at: string;
}

export interface AdminStatsOverviewResponse {
  user_count: number;
  file_count: number;
  storage_size: number;
  share_count: number;
  active_share_count: number;
  storage_quota: number;
}

export interface AdminStatsSystemResponse {
  uptime: number;
  version: string;
  mysql: {
    connected: boolean;
    connection_count: number;
    latency_ms: number;
  };
  redis: {
    connected: boolean;
    latency_ms: number;
  };
  disk: {
    total: number;
    used: number;
    free: number;
    percentage: number;
  };
}
```

---

## 11. 错误码对照表

### 通用错误码

| 错误码 | 枚举名称 | HTTP 状态码 | 说明 |
|--------|----------|------------|------|
| 0 | `Success` | 200 | 成功 |
| 10001 | `InvalidParameter` | 400 | 请求参数错误 |
| 10002 | `ValidationFailed` | 400 | 参数校验失败 |
| 10003 | `ResourceNotFound` | 404 | 资源不存在 |
| 10004 | `ResourceConflict` | 409 | 资源冲突 |
| 10005 | `TooManyRequests` | 429 | 请求过于频繁 |
| 10006 | `InternalError` | 500 | 服务器内部错误 |

### 认证错误码

| 错误码 | 枚举名称 | HTTP 状态码 | 说明 |
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

### 文件错误码

| 错误码 | 枚举名称 | HTTP 状态码 | 说明 |
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

### 分享错误码

| 错误码 | 枚举名称 | HTTP 状态码 | 说明 |
|--------|----------|------------|------|
| 60001 | `ShareNotFound` | 404 | 分享不存在 |
| 60002 | `ShareExpired` | 400 | 分享已过期 |
| 60003 | `SharePasswordError` | 400 | 分享密码错误 |
| 60004 | `ShareAccessDenied` | 403 | 无权限访问 |

### Redis 错误码

| 错误码 | 枚举名称 | HTTP 状态码 | 说明 |
|--------|----------|------------|------|
| 70002 | `RedisOperationFailed` | 500 | Redis 操作失败 |
| 70003 | `RedisKeyNotFound` | 404 | Redis key 不存在 |

### 管理员错误码

| 错误码 | 枚举名称 | HTTP 状态码 | 说明 |
|--------|----------|------------|------|
| 80001 | `AdminRequired` | 403 | 需要管理员权限 |
| 80002 | `AdminUserNotFound` | 404 | 用户不存在 |
| 80003 | `AdminCannotModifySelf` | 400 | 不能修改自己的状态或角色 |
| 80004 | `AdminCannotDemoteLast` | 400 | 不能降级最后一个管理员 |
| 80005 | `AdminShareNotFound` | 404 | 分享不存在 |
| 80006 | `AdminInvalidStatus` | 400 | 无效的用户状态 |
| 80007 | `AdminInvalidRole` | 400 | 无效的角色 |

---

## 12. 特殊行为说明

### 12.1 分块上传流程

前端实现分块上传时，需要按以下顺序调用接口：

1. **初始化上传** `POST /api/file/upload/init`
   - 计算文件 MD5 作为 `file_hash`
   - 如果返回 `instant_upload: true`，表示秒传成功，直接结束
   - 否则获取 `upload_id`、`chunk_size`、`total_chunks` 和 `uploaded_chunks`（断点续传）

2. **上传分片** `POST /api/file/upload/chunk`
   - 请求头 `Content-Type: application/octet-stream`
   - 查询参数携带 `upload_id`、`chunk_index`、`chunk_hash`
   - 请求体为二进制分片数据
   - 跳过 `uploaded_chunks` 中已存在的索引

3. **完成上传** `POST /api/file/upload/complete`
   - 携带 `upload_id`
   - 服务端合并分片并返回文件信息

4. **取消上传**（可选）`DELETE /api/file/upload/{upload_id}`
   - 用户主动取消或页面卸载时调用
   - 释放预占用的存储空间

### 12.2 批量操作语义

以下接口支持批量操作，采用**确定性混合结果契约**：无论请求中多少项成功或失败，HTTP 状态码始终返回 `200 OK`，通过响应体中的 `summary` 和 `results` 表达每项的处理结果。

- `PUT /api/file/move`
- `POST /api/file/copy`
- `DELETE /api/file`
- `POST /api/trash/restore`
- `DELETE /api/trash`
- `DELETE /api/share`

### 12.3 双认证域

前端 Axios 拦截器需要实现双认证域切换逻辑：

```typescript
// Owner 域（Bearer JWT）
axios.defaults.headers.common['Authorization'] = `Bearer ${accessToken}`;

// Visitor 域（Share Token）
// 访问分享相关接口时，移除 Authorization，添加 X-Share-Token
axios.defaults.headers.common['X-Share-Token'] = shareToken;
delete axios.defaults.headers.common['Authorization'];
```

**注意**：Owner 和 Visitor 两个认证域的令牌绝不能混用。`RequestFactory` 应强制分离两种请求上下文。

### 12.4 Range 下载

前端实现断点续传下载时，需要在请求头中添加 `Range` 字段：

```typescript
// 下载前 1MB
headers: { 'Range': 'bytes=0-1048575' }

// 从 1MB 位置下载到末尾
headers: { 'Range': 'bytes=1048576-' }
```

服务端响应：
- 无 Range：`200 OK`，返回完整文件
- 有效 Range：`206 Partial Content`，返回指定范围
- 无效 Range：`416 Range Not Satisfiable`

分享下载接口 `GET /api/share/download/{share_id}/{file_id}` 同样支持 Range 请求。

### 12.5 存储配额语义

前端上传前需要理解配额计算方式：

```
有效可用 = quota - used - reserved
```

- `used`：实际已使用（含回收站文件）
- `reserved`：已预留但未完成的上传空间
- `quota`：用户总存储配额

上传生命周期配额变化：
- `init`：`reserved += file_size`（预占用）
- `chunk`：无变化
- `complete`：`reserved -= file_size`，`used += file_size`
- `cancel`：`reserved -= file_size`（释放）

### 12.6 分享访问安全

- 分享密码错误 5 次后，该 IP 对该分享锁定 15 分钟
- Share Token 有效期默认 1 小时（3600 秒）
- 分享被取消或过期后，已签发的 Share Token 立即失效
- 密码验证失败响应统一返回 `60003`，不泄露分享是否存在

---

*文档版本: 1.0.0*  
*基于后端 API 设计文档: docs/design/02-API接口设计.md*  
*DTO 参考: src/dtos/{Auth,User,File,Folder,Share,Trash,Admin}Dto.hpp*
