// ==================== 上传 ====================

/** 初始化上传请求 */
export interface InitUploadRequest {
  /** 文件名：1-255 字符，禁止特殊字符 */
  readonly filename: string;
  /** 文件大小（字节），必须 > 0 */
  readonly file_size: number;
  /** 32 字符小写十六进制 MD5 */
  readonly file_hash: string;
  /** 父目录 ID，默认 0（根目录） */
  parent_id?: number;
}

/** 秒传文件信息 */
export interface InstantFileInfo {
  readonly id: number;
  readonly name: string;
  readonly size: number;
  readonly hash: string;
  readonly mime_type: string;
  readonly parent_id: number;
  readonly created_at: string;
}

/** 初始化上传响应 */
export interface InitUploadResponse {
  /** 上传会话 ID（秒传时为空） */
  readonly upload_id: string;
  /** 分片大小（字节） */
  readonly chunk_size: number;
  /** 总分片数 */
  readonly total_chunks: number;
  /** 已上传的分片索引（断点续传） */
  readonly uploaded_chunks: readonly number[];
  /** 是否秒传 */
  readonly instant_upload: boolean;
  /** 秒传时返回的文件信息 */
  readonly file?: InstantFileInfo;
}

/** 分片上传查询参数 */
export interface UploadChunkQuery {
  /** 上传会话 ID */
  readonly upload_id: string;
  /** 分片索引（从 0 开始） */
  readonly chunk_index: number;
  /** 分片 MD5 哈希（32 字符小写十六进制） */
  readonly chunk_hash: string;
}

/** 分片上传响应 */
export interface UploadChunkResponse {
  readonly chunk_index: number;
  readonly uploaded: boolean;
}

/** 完成上传请求 */
export interface CompleteUploadRequest {
  readonly upload_id: string;
}

/** 完成上传后的文件信息 */
export interface UploadedFileInfo {
  readonly id: number;
  readonly name: string;
  readonly size: number;
  readonly hash: string;
  readonly mime_type: string;
  readonly parent_id: number;
  readonly created_at: string;
}

/** 完成上传响应 */
export interface CompleteUploadResponse {
  readonly file: UploadedFileInfo;
}

// ==================== 下载 ====================

/** 下载信息响应 */
export interface DownloadInfoResponse {
  readonly file_id: number;
  readonly filename: string;
  readonly file_size: number;
  readonly file_hash: string;
  readonly mime_type: string;
  readonly supports_range: boolean;
}

// ==================== 客户端上传任务状态 ====================

/** 上传任务状态 */
export type UploadTaskStatus =
  | 'queued'
  | 'hashing'
  | 'uploading'
  | 'completing'
  | 'completed'
  | 'failed'
  | 'cancelled';

/** 客户端上传任务（运行时状态，非 API 响应） */
export interface UploadTask {
  /** 客户端任务 ID */
  readonly id: string;
  /** 原始 File 对象 */
  readonly file: File;
  /** 文件名 */
  readonly file_name: string;
  /** 文件大小（字节） */
  readonly file_size: number;
  /** 当前状态 */
  status: UploadTaskStatus;
  /** 上传进度 0-100 */
  progress: number;
  /** 已上传的分片索引集合 */
  uploaded_chunks: Set<number>;
  /** 总分片数 */
  readonly total_chunks: number;
  /** 服务端上传会话 ID */
  upload_id?: string;
  /** 目标父目录 ID */
  readonly parent_id: number;
  /** 错误信息 */
  error?: string;
}

// ==================== 客户端下载任务状态 ====================

/** 下载任务状态 */
export type DownloadTaskStatus =
  | 'pending'
  | 'downloading'
  | 'paused'
  | 'completed'
  | 'failed'
  | 'cancelled';

/** 客户端下载任务（运行时状态，非 API 响应） */
export interface DownloadTask {
  /** 客户端任务 ID */
  readonly id: string;
  /** 文件 ID */
  readonly file_id: string;
  /** 文件名 */
  readonly file_name: string;
  /** 文件大小（字节） */
  readonly file_size: number;
  /** 当前状态 */
  status: DownloadTaskStatus;
  /** 下载进度 0-100 */
  progress: number;
  /** 已接收字节数 */
  received_bytes: number;
  /** 服务端报告的总字节数 */
  total_size: number;
  /** 是否支持 Range 续传 */
  supports_range: boolean;
  /** 运行时已接收分片，用于暂停后继续拼接 */
  chunks: Uint8Array[];
  /** 错误信息 */
  error?: string;
}
