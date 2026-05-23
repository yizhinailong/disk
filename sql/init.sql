-- 用户表
CREATE TABLE users (
    id BIGSERIAL PRIMARY KEY,
    username VARCHAR(32) NOT NULL,
    email VARCHAR(128) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    nickname VARCHAR(64) DEFAULT NULL,
    avatar VARCHAR(512) DEFAULT NULL,
    storage_quota BIGINT NOT NULL DEFAULT 10737418240,
    storage_used BIGINT NOT NULL DEFAULT 0,
    storage_reserved BIGINT NOT NULL DEFAULT 0,
    status SMALLINT NOT NULL DEFAULT 1,
    role SMALLINT NOT NULL DEFAULT 0,
    login_attempts INTEGER NOT NULL DEFAULT 0,
    locked_until TIMESTAMP DEFAULT NULL,
    last_login_at TIMESTAMP DEFAULT NULL,
    last_login_ip VARCHAR(45) DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT uk_users_username UNIQUE (username),
    CONSTRAINT uk_users_email UNIQUE (email)
);

CREATE INDEX idx_users_status ON users (status);
CREATE INDEX idx_users_role ON users (role);
CREATE INDEX idx_users_created_at ON users (created_at);

COMMENT ON TABLE users IS '用户表';
COMMENT ON COLUMN users.id IS '用户ID';
COMMENT ON COLUMN users.username IS '用户名';
COMMENT ON COLUMN users.email IS '邮箱';
COMMENT ON COLUMN users.password_hash IS '密码哈希';
COMMENT ON COLUMN users.nickname IS '昵称';
COMMENT ON COLUMN users.avatar IS '头像URL';
COMMENT ON COLUMN users.storage_quota IS '存储配额(字节)，默认10GB';
COMMENT ON COLUMN users.storage_used IS '已用存储(字节)';
COMMENT ON COLUMN users.storage_reserved IS '上传预占用存储(字节)';
COMMENT ON COLUMN users.status IS '状态: 0-禁用, 1-正常, 2-锁定';
COMMENT ON COLUMN users.role IS '角色: 0-普通用户, 1-管理员';
COMMENT ON COLUMN users.login_attempts IS '登录失败次数';
COMMENT ON COLUMN users.locked_until IS '锁定截止时间';
COMMENT ON COLUMN users.last_login_at IS '最后登录时间';
COMMENT ON COLUMN users.last_login_ip IS '最后登录IP';
COMMENT ON COLUMN users.created_at IS '创建时间';
COMMENT ON COLUMN users.updated_at IS '更新时间';

-- 文件内容表
CREATE TABLE file_contents (
    id BIGSERIAL PRIMARY KEY,
    hash_md5 CHAR(32) NOT NULL,
    hash_sha256 CHAR(64) NOT NULL,
    size BIGINT NOT NULL,
    storage_path VARCHAR(512) NOT NULL,
    mime_type VARCHAR(128) DEFAULT NULL,
    ref_count INTEGER NOT NULL DEFAULT 1,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT uk_file_contents_hash UNIQUE (hash_md5, hash_sha256)
);

CREATE INDEX idx_file_contents_md5 ON file_contents (hash_md5);
CREATE INDEX idx_file_contents_ref_count ON file_contents (ref_count);

COMMENT ON TABLE file_contents IS '文件内容表';
COMMENT ON COLUMN file_contents.id IS '内容ID';
COMMENT ON COLUMN file_contents.hash_md5 IS 'MD5哈希';
COMMENT ON COLUMN file_contents.hash_sha256 IS 'SHA256哈希';
COMMENT ON COLUMN file_contents.size IS '文件大小(字节)';
COMMENT ON COLUMN file_contents.storage_path IS '存储路径';
COMMENT ON COLUMN file_contents.mime_type IS 'MIME类型';
COMMENT ON COLUMN file_contents.ref_count IS '引用计数';
COMMENT ON COLUMN file_contents.created_at IS '创建时间';

-- 文件夹表
CREATE TABLE folders (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL,
    parent_id BIGINT NOT NULL DEFAULT 0,
    name VARCHAR(255) NOT NULL,
    path VARCHAR(4096) NOT NULL DEFAULT '/',
    depth INTEGER NOT NULL DEFAULT 0,
    item_count INTEGER NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT uk_folders_user_parent_name UNIQUE (user_id, parent_id, name),
    CONSTRAINT fk_folders_user_id FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE
);

CREATE INDEX idx_folders_user_id ON folders (user_id);
CREATE INDEX idx_folders_parent_id ON folders (parent_id);
CREATE INDEX idx_folders_user_parent ON folders (user_id, parent_id);
CREATE INDEX idx_folders_user_parent_created_name_id ON folders (user_id, parent_id, created_at, name, id);
CREATE INDEX idx_folders_user_parent_updated_name_id ON folders (user_id, parent_id, updated_at, name, id);
CREATE INDEX idx_folders_user_name_id ON folders (user_id, name, id);
CREATE INDEX ft_folders_name ON folders USING GIN (to_tsvector('simple', name));

COMMENT ON TABLE folders IS '文件夹表';
COMMENT ON COLUMN folders.id IS '文件夹ID';
COMMENT ON COLUMN folders.user_id IS '所属用户ID';
COMMENT ON COLUMN folders.parent_id IS '父文件夹ID，0表示根目录';
COMMENT ON COLUMN folders.name IS '文件夹名称';
COMMENT ON COLUMN folders.path IS '完整路径';
COMMENT ON COLUMN folders.depth IS '目录深度';
COMMENT ON COLUMN folders.item_count IS '子项数量(文件+文件夹)';
COMMENT ON COLUMN folders.created_at IS '创建时间';
COMMENT ON COLUMN folders.updated_at IS '更新时间';

-- 文件表
CREATE TABLE files (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL,
    content_id BIGINT NOT NULL,
    folder_id BIGINT NOT NULL DEFAULT 0,
    name VARCHAR(255) NOT NULL,
    extension VARCHAR(32) DEFAULT NULL,
    size BIGINT NOT NULL,
    mime_type VARCHAR(128) DEFAULT NULL,
    path VARCHAR(4096) NOT NULL DEFAULT '/',
    is_favorite SMALLINT NOT NULL DEFAULT 0,
    download_count INTEGER NOT NULL DEFAULT 0,
    last_accessed_at TIMESTAMP DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT uk_files_user_folder_name UNIQUE (user_id, folder_id, name),
    CONSTRAINT fk_files_user_id FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE,
    CONSTRAINT fk_files_content_id FOREIGN KEY (content_id) REFERENCES file_contents (id)
);

CREATE INDEX idx_files_user_id ON files (user_id);
CREATE INDEX idx_files_folder_id ON files (folder_id);
CREATE INDEX idx_files_content_id ON files (content_id);
CREATE INDEX idx_files_user_folder ON files (user_id, folder_id);
CREATE INDEX idx_files_user_folder_size_name_id ON files (user_id, folder_id, size, name, id);
CREATE INDEX idx_files_user_folder_created_name_id ON files (user_id, folder_id, created_at, name, id);
CREATE INDEX idx_files_user_folder_updated_name_id ON files (user_id, folder_id, updated_at, name, id);
CREATE INDEX idx_files_user_name_id ON files (user_id, name, id);
CREATE INDEX idx_files_extension ON files (extension);
CREATE INDEX idx_files_created_at ON files (created_at);
CREATE INDEX ft_files_name ON files USING GIN (to_tsvector('simple', name));

COMMENT ON TABLE files IS '文件表';
COMMENT ON COLUMN files.id IS '文件ID';
COMMENT ON COLUMN files.user_id IS '所属用户ID';
COMMENT ON COLUMN files.content_id IS '文件内容ID';
COMMENT ON COLUMN files.folder_id IS '所属文件夹ID，0表示根目录';
COMMENT ON COLUMN files.name IS '文件名';
COMMENT ON COLUMN files.extension IS '文件扩展名';
COMMENT ON COLUMN files.size IS '文件大小(字节)';
COMMENT ON COLUMN files.mime_type IS 'MIME类型';
COMMENT ON COLUMN files.path IS '完整路径';
COMMENT ON COLUMN files.is_favorite IS '是否收藏: 0-否, 1-是';
COMMENT ON COLUMN files.download_count IS '下载次数';
COMMENT ON COLUMN files.last_accessed_at IS '最后访问时间';
COMMENT ON COLUMN files.created_at IS '创建时间';
COMMENT ON COLUMN files.updated_at IS '更新时间';

-- 上传任务表
CREATE TABLE upload_tasks (
    id VARCHAR(64) NOT NULL PRIMARY KEY,
    user_id BIGINT NOT NULL,
    folder_id BIGINT NOT NULL DEFAULT 0,
    filename VARCHAR(255) NOT NULL,
    file_size BIGINT NOT NULL,
    file_hash CHAR(32) NOT NULL,
    chunk_size INTEGER NOT NULL,
    total_chunks INTEGER NOT NULL,
    reserved_bytes BIGINT NOT NULL DEFAULT 0,
    temp_path VARCHAR(512) NOT NULL,
    status SMALLINT NOT NULL DEFAULT 0,
    expires_at TIMESTAMP NOT NULL,
    finalized_at TIMESTAMP DEFAULT NULL,
    fail_reason VARCHAR(512) DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_upload_tasks_user_id FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE
);

CREATE INDEX idx_upload_tasks_user_id ON upload_tasks (user_id);
CREATE INDEX idx_upload_tasks_status ON upload_tasks (status);
CREATE INDEX idx_upload_tasks_expires_at ON upload_tasks (expires_at);
CREATE INDEX idx_upload_tasks_user_hash ON upload_tasks (user_id, file_hash);
CREATE INDEX idx_upload_tasks_status_expires ON upload_tasks (status, expires_at);
CREATE INDEX idx_upload_tasks_user_status ON upload_tasks (user_id, status);

COMMENT ON TABLE upload_tasks IS '上传任务表';
COMMENT ON COLUMN upload_tasks.id IS '上传任务ID';
COMMENT ON COLUMN upload_tasks.user_id IS '用户ID';
COMMENT ON COLUMN upload_tasks.folder_id IS '目标文件夹ID';
COMMENT ON COLUMN upload_tasks.filename IS '文件名';
COMMENT ON COLUMN upload_tasks.file_size IS '文件大小';
COMMENT ON COLUMN upload_tasks.file_hash IS '文件MD5哈希';
COMMENT ON COLUMN upload_tasks.chunk_size IS '分片大小';
COMMENT ON COLUMN upload_tasks.total_chunks IS '总分片数';
COMMENT ON COLUMN upload_tasks.reserved_bytes IS '预占用字节数';
COMMENT ON COLUMN upload_tasks.temp_path IS '临时存储路径';
COMMENT ON COLUMN upload_tasks.status IS '状态: 0-进行中, 1-已完成, 2-已取消, 3-已过期';
COMMENT ON COLUMN upload_tasks.expires_at IS '过期时间';
COMMENT ON COLUMN upload_tasks.finalized_at IS '完成/失败时间';
COMMENT ON COLUMN upload_tasks.fail_reason IS '失败原因';
COMMENT ON COLUMN upload_tasks.created_at IS '创建时间';
COMMENT ON COLUMN upload_tasks.updated_at IS '更新时间';

-- 上传任务分片表
CREATE TABLE upload_task_chunks (
    task_id VARCHAR(64) NOT NULL,
    chunk_index INTEGER NOT NULL,
    uploaded_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (task_id, chunk_index),
    CONSTRAINT fk_upload_task_chunks_task_id FOREIGN KEY (task_id) REFERENCES upload_tasks (id) ON DELETE CASCADE
);

CREATE INDEX idx_upload_task_chunks_task_id ON upload_task_chunks (task_id);

COMMENT ON TABLE upload_task_chunks IS '上传任务分片表';
COMMENT ON COLUMN upload_task_chunks.task_id IS '上传任务ID';
COMMENT ON COLUMN upload_task_chunks.chunk_index IS '分片索引(从0开始)';
COMMENT ON COLUMN upload_task_chunks.uploaded_at IS '上传时间';

-- 回收站表
CREATE TABLE trash (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL,
    item_type VARCHAR(10) NOT NULL CHECK (item_type IN ('file', 'folder')),
    item_id BIGINT NOT NULL,
    item_name VARCHAR(255) NOT NULL,
    item_size BIGINT NOT NULL DEFAULT 0,
    content_id BIGINT DEFAULT NULL,
    original_folder_id BIGINT NOT NULL DEFAULT 0,
    original_path VARCHAR(4096) NOT NULL,
    item_data JSONB,
    deleted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    CONSTRAINT fk_trash_user_id FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE,
    CONSTRAINT fk_trash_content_id FOREIGN KEY (content_id) REFERENCES file_contents (id) ON DELETE SET NULL
);

CREATE INDEX idx_trash_user_id ON trash (user_id);
CREATE INDEX idx_trash_item_type ON trash (item_type);
CREATE INDEX idx_trash_deleted_at ON trash (deleted_at);
CREATE INDEX idx_trash_expires_at ON trash (expires_at);
CREATE INDEX idx_trash_content_id ON trash (content_id);

COMMENT ON TABLE trash IS '回收站表';
COMMENT ON COLUMN trash.id IS '回收站记录ID';
COMMENT ON COLUMN trash.user_id IS '用户ID';
COMMENT ON COLUMN trash.item_type IS '项目类型';
COMMENT ON COLUMN trash.item_id IS '原项目ID';
COMMENT ON COLUMN trash.item_name IS '项目名称';
COMMENT ON COLUMN trash.item_size IS '项目大小';
COMMENT ON COLUMN trash.content_id IS '关联的文件内容ID(结构化引用)';
COMMENT ON COLUMN trash.original_folder_id IS '原所属文件夹ID';
COMMENT ON COLUMN trash.original_path IS '原完整路径';
COMMENT ON COLUMN trash.item_data IS '项目完整数据备份';
COMMENT ON COLUMN trash.deleted_at IS '删除时间';
COMMENT ON COLUMN trash.expires_at IS '过期时间(彻底删除时间)';

-- 分享表
CREATE TABLE shares (
    id BIGSERIAL PRIMARY KEY,
    share_code VARCHAR(32) NOT NULL,
    user_id BIGINT NOT NULL,
    password_hash VARCHAR(255) DEFAULT NULL,
    permission VARCHAR(10) NOT NULL DEFAULT 'download' CHECK (permission IN ('view', 'download')),
    view_count INTEGER NOT NULL DEFAULT 0,
    download_count INTEGER NOT NULL DEFAULT 0,
    status SMALLINT NOT NULL DEFAULT 1,
    expires_at TIMESTAMP DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT uk_shares_code UNIQUE (share_code),
    CONSTRAINT fk_shares_user_id FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE
);

CREATE INDEX idx_shares_user_id ON shares (user_id);
CREATE INDEX idx_shares_status ON shares (status);
CREATE INDEX idx_shares_expires_at ON shares (expires_at);

COMMENT ON TABLE shares IS '分享表';
COMMENT ON COLUMN shares.id IS '分享ID';
COMMENT ON COLUMN shares.share_code IS '分享短码';
COMMENT ON COLUMN shares.user_id IS '分享者用户ID';
COMMENT ON COLUMN shares.password_hash IS '访问密码哈希';
COMMENT ON COLUMN shares.permission IS '权限';
COMMENT ON COLUMN shares.view_count IS '访问次数';
COMMENT ON COLUMN shares.download_count IS '下载次数';
COMMENT ON COLUMN shares.status IS '状态: 0-已取消, 1-有效, 2-已过期';
COMMENT ON COLUMN shares.expires_at IS '过期时间，NULL表示永久';
COMMENT ON COLUMN shares.created_at IS '创建时间';
COMMENT ON COLUMN shares.updated_at IS '更新时间';

-- 分享文件关联表
CREATE TABLE share_files (
    id BIGSERIAL PRIMARY KEY,
    share_id BIGINT NOT NULL,
    item_type VARCHAR(10) NOT NULL CHECK (item_type IN ('file', 'folder')),
    item_id BIGINT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT uk_share_files_share_item UNIQUE (share_id, item_type, item_id),
    CONSTRAINT fk_share_files_share_id FOREIGN KEY (share_id) REFERENCES shares (id) ON DELETE CASCADE
);

CREATE INDEX idx_share_files_share_id ON share_files (share_id);

COMMENT ON TABLE share_files IS '分享文件关联表';
COMMENT ON COLUMN share_files.id IS '记录ID';
COMMENT ON COLUMN share_files.share_id IS '分享ID';
COMMENT ON COLUMN share_files.item_type IS '项目类型';
COMMENT ON COLUMN share_files.item_id IS '文件/文件夹ID';
COMMENT ON COLUMN share_files.created_at IS '创建时间';

-- 操作日志表
CREATE TABLE operation_logs (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL,
    action VARCHAR(32) NOT NULL,
    target_type VARCHAR(32) DEFAULT NULL,
    target_id BIGINT DEFAULT NULL,
    target_name VARCHAR(255) DEFAULT NULL,
    details JSONB DEFAULT NULL,
    ip_address VARCHAR(45) NOT NULL,
    user_agent VARCHAR(512) DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_operation_logs_user_id FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE
);

CREATE INDEX idx_operation_logs_user_id ON operation_logs (user_id);
CREATE INDEX idx_operation_logs_action ON operation_logs (action);
CREATE INDEX idx_operation_logs_created_at ON operation_logs (created_at);
CREATE INDEX idx_operation_logs_target ON operation_logs (target_type, target_id);

COMMENT ON TABLE operation_logs IS '操作日志表';
COMMENT ON COLUMN operation_logs.id IS '日志ID';
COMMENT ON COLUMN operation_logs.user_id IS '用户ID';
COMMENT ON COLUMN operation_logs.action IS '操作类型';
COMMENT ON COLUMN operation_logs.target_type IS '目标类型';
COMMENT ON COLUMN operation_logs.target_id IS '目标ID';
COMMENT ON COLUMN operation_logs.target_name IS '目标名称';
COMMENT ON COLUMN operation_logs.details IS '操作详情';
COMMENT ON COLUMN operation_logs.ip_address IS 'IP地址';
COMMENT ON COLUMN operation_logs.user_agent IS '客户端信息';
COMMENT ON COLUMN operation_logs.created_at IS '创建时间';

-- 创建默认管理员用户 密码为 Admin123
INSERT INTO users (username, email, password_hash, nickname, storage_quota, role)
VALUES ('admin', 'admin@example.com', '$argon2id$v=19$m=65536,t=2,p=1$BjgpFYz8h/yjnJYjV497Tw$JCgRPDFvioq+FPQuR0i3a6kiTnLALv/F1A0eim7x7zE', '管理员', 107374182400, 1);
