-- 创建数据库
CREATE DATABASE IF NOT EXISTS `disk`
CHARACTER SET utf8mb4
COLLATE utf8mb4_unicode_ci;

USE `disk`;

-- 用户表
CREATE TABLE `users` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '用户ID',
    `username` VARCHAR(32) NOT NULL COMMENT '用户名',
    `email` VARCHAR(128) NOT NULL COMMENT '邮箱',
    `password_hash` VARCHAR(255) NOT NULL COMMENT '密码哈希',
    `nickname` VARCHAR(64) DEFAULT NULL COMMENT '昵称',
    `avatar` VARCHAR(512) DEFAULT NULL COMMENT '头像URL',
    `storage_quota` BIGINT UNSIGNED NOT NULL DEFAULT 10737418240 COMMENT '存储配额(字节)，默认10GB',
    `storage_used` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '已用存储(字节)',
    `storage_reserved` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '上传预占用存储(字节)',
    `status` TINYINT NOT NULL DEFAULT 1 COMMENT '状态: 0-禁用, 1-正常, 2-锁定',
    `login_attempts` INT NOT NULL DEFAULT 0 COMMENT '登录失败次数',
    `locked_until` DATETIME DEFAULT NULL COMMENT '锁定截止时间',
    `last_login_at` DATETIME DEFAULT NULL COMMENT '最后登录时间',
    `last_login_ip` VARCHAR(45) DEFAULT NULL COMMENT '最后登录IP',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_users_username` (`username`),
    UNIQUE KEY `uk_users_email` (`email`),
    KEY `idx_users_status` (`status`),
    KEY `idx_users_created_at` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='用户表';

-- 文件内容表
CREATE TABLE `file_contents` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '内容ID',
    `hash_md5` CHAR(32) NOT NULL COMMENT 'MD5哈希',
    `hash_sha256` CHAR(64) NOT NULL COMMENT 'SHA256哈希',
    `size` BIGINT UNSIGNED NOT NULL COMMENT '文件大小(字节)',
    `storage_path` VARCHAR(512) NOT NULL COMMENT '存储路径',
    `mime_type` VARCHAR(128) DEFAULT NULL COMMENT 'MIME类型',
    `ref_count` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT '引用计数',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_file_contents_hash` (`hash_md5`, `hash_sha256`),
    KEY `idx_file_contents_md5` (`hash_md5`),
    KEY `idx_file_contents_ref_count` (`ref_count`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='文件内容表';

-- 文件夹表
CREATE TABLE `folders` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '文件夹ID',
    `user_id` BIGINT UNSIGNED NOT NULL COMMENT '所属用户ID',
    `parent_id` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '父文件夹ID，0表示根目录',
    `name` VARCHAR(255) NOT NULL COMMENT '文件夹名称',
    `path` VARCHAR(4096) NOT NULL DEFAULT '/' COMMENT '完整路径',
    `depth` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '目录深度',
    `item_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '子项数量(文件+文件夹)',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    KEY `idx_folders_user_id` (`user_id`),
    KEY `idx_folders_parent_id` (`parent_id`),
    KEY `idx_folders_user_parent` (`user_id`, `parent_id`),
    FULLTEXT KEY `ft_folders_name` (`name`),
    UNIQUE KEY `uk_folders_user_parent_name` (`user_id`, `parent_id`, `name`),
    CONSTRAINT `fk_folders_user_id` FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='文件夹表';

-- 文件表
CREATE TABLE `files` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '文件ID',
    `user_id` BIGINT UNSIGNED NOT NULL COMMENT '所属用户ID',
    `content_id` BIGINT UNSIGNED NOT NULL COMMENT '文件内容ID',
    `folder_id` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '所属文件夹ID，0表示根目录',
    `name` VARCHAR(255) NOT NULL COMMENT '文件名',
    `extension` VARCHAR(32) DEFAULT NULL COMMENT '文件扩展名',
    `size` BIGINT UNSIGNED NOT NULL COMMENT '文件大小(字节)',
    `mime_type` VARCHAR(128) DEFAULT NULL COMMENT 'MIME类型',
    `path` VARCHAR(4096) NOT NULL DEFAULT '/' COMMENT '完整路径',
    `is_favorite` TINYINT NOT NULL DEFAULT 0 COMMENT '是否收藏: 0-否, 1-是',
    `download_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '下载次数',
    `last_accessed_at` DATETIME DEFAULT NULL COMMENT '最后访问时间',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    KEY `idx_files_user_id` (`user_id`),
    KEY `idx_files_folder_id` (`folder_id`),
    KEY `idx_files_content_id` (`content_id`),
    KEY `idx_files_user_folder` (`user_id`, `folder_id`),
    KEY `idx_files_extension` (`extension`),
    KEY `idx_files_created_at` (`created_at`),
    FULLTEXT KEY `ft_files_name` (`name`),
    UNIQUE KEY `uk_files_user_folder_name` (`user_id`, `folder_id`, `name`),
    CONSTRAINT `fk_files_user_id` FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_files_content_id` FOREIGN KEY (`content_id`) REFERENCES `file_contents` (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='文件表';

-- 上传任务表
CREATE TABLE `upload_tasks` (
    `id` VARCHAR(64) NOT NULL COMMENT '上传任务ID',
    `user_id` BIGINT UNSIGNED NOT NULL COMMENT '用户ID',
    `folder_id` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '目标文件夹ID',
    `filename` VARCHAR(255) NOT NULL COMMENT '文件名',
    `file_size` BIGINT UNSIGNED NOT NULL COMMENT '文件大小',
    `file_hash` CHAR(32) NOT NULL COMMENT '文件MD5哈希',
    `chunk_size` INT UNSIGNED NOT NULL COMMENT '分片大小',
    `total_chunks` INT UNSIGNED NOT NULL COMMENT '总分片数',
    `uploaded_chunks` TEXT COMMENT '已上传分片索引列表(JSON数组)【已废弃，兼容性保留】',
    `reserved_bytes` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '预占用字节数',
    `temp_path` VARCHAR(512) NOT NULL COMMENT '临时存储路径',
    `status` TINYINT NOT NULL DEFAULT 0 COMMENT '状态: 0-进行中, 1-已完成, 2-已取消, 3-已过期',
    `expires_at` DATETIME NOT NULL COMMENT '过期时间',
    `finalized_at` DATETIME DEFAULT NULL COMMENT '完成/失败时间',
    `fail_reason` VARCHAR(512) DEFAULT NULL COMMENT '失败原因',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    KEY `idx_upload_tasks_user_id` (`user_id`),
    KEY `idx_upload_tasks_status` (`status`),
    KEY `idx_upload_tasks_expires_at` (`expires_at`),
    KEY `idx_upload_tasks_user_hash` (`user_id`, `file_hash`),
    KEY `idx_upload_tasks_status_expires` (`status`, `expires_at`),
    KEY `idx_upload_tasks_user_status` (`user_id`, `status`),
    CONSTRAINT `fk_upload_tasks_user_id` FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='上传任务表';

-- 上传任务分片表
CREATE TABLE `upload_task_chunks` (
    `task_id` VARCHAR(64) NOT NULL COMMENT '上传任务ID',
    `chunk_index` INT UNSIGNED NOT NULL COMMENT '分片索引(从0开始)',
    `uploaded_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '上传时间',
    PRIMARY KEY (`task_id`, `chunk_index`),
    KEY `idx_upload_task_chunks_task_id` (`task_id`),
    CONSTRAINT `fk_upload_task_chunks_task_id` FOREIGN KEY (`task_id`) REFERENCES `upload_tasks` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='上传任务分片表';

-- 回收站表
CREATE TABLE `trash` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '回收站记录ID',
    `user_id` BIGINT UNSIGNED NOT NULL COMMENT '用户ID',
    `item_type` ENUM('file', 'folder') NOT NULL COMMENT '项目类型',
    `item_id` BIGINT UNSIGNED NOT NULL COMMENT '原项目ID',
    `item_name` VARCHAR(255) NOT NULL COMMENT '项目名称',
    `item_size` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '项目大小',
    `content_id` BIGINT UNSIGNED DEFAULT NULL COMMENT '关联的文件内容ID(结构化引用)',
    `original_folder_id` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '原所属文件夹ID',
    `original_path` VARCHAR(4096) NOT NULL COMMENT '原完整路径',
    `item_data` JSON COMMENT '项目完整数据备份',
    `deleted_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '删除时间',
    `expires_at` DATETIME NOT NULL COMMENT '过期时间(彻底删除时间)',
    PRIMARY KEY (`id`),
    KEY `idx_trash_user_id` (`user_id`),
    KEY `idx_trash_item_type` (`item_type`),
    KEY `idx_trash_deleted_at` (`deleted_at`),
    KEY `idx_trash_expires_at` (`expires_at`),
    KEY `idx_trash_content_id` (`content_id`),
    CONSTRAINT `fk_trash_user_id` FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_trash_content_id` FOREIGN KEY (`content_id`) REFERENCES `file_contents` (`id`) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='回收站表';

-- 分享表
CREATE TABLE `shares` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '分享ID',
    `share_code` VARCHAR(32) NOT NULL COMMENT '分享短码',
    `user_id` BIGINT UNSIGNED NOT NULL COMMENT '分享者用户ID',
    `password_hash` VARCHAR(255) DEFAULT NULL COMMENT '访问密码哈希',
    `permission` ENUM('view', 'download') NOT NULL DEFAULT 'download' COMMENT '权限',
    `view_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '访问次数',
    `download_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '下载次数',
    `status` TINYINT NOT NULL DEFAULT 1 COMMENT '状态: 0-已取消, 1-有效, 2-已过期',
    `expires_at` DATETIME DEFAULT NULL COMMENT '过期时间，NULL表示永久',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_shares_code` (`share_code`),
    KEY `idx_shares_user_id` (`user_id`),
    KEY `idx_shares_status` (`status`),
    KEY `idx_shares_expires_at` (`expires_at`),
    CONSTRAINT `fk_shares_user_id` FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='分享表';

-- 分享文件关联表
CREATE TABLE `share_files` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '记录ID',
    `share_id` BIGINT UNSIGNED NOT NULL COMMENT '分享ID',
    `item_type` ENUM('file', 'folder') NOT NULL COMMENT '项目类型',
    `item_id` BIGINT UNSIGNED NOT NULL COMMENT '文件/文件夹ID',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_share_files_share_item` (`share_id`, `item_type`, `item_id`),
    KEY `idx_share_files_share_id` (`share_id`),
    CONSTRAINT `fk_share_files_share_id` FOREIGN KEY (`share_id`) REFERENCES `shares` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='分享文件关联表';

-- 操作日志表
CREATE TABLE `operation_logs` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '日志ID',
    `user_id` BIGINT UNSIGNED NOT NULL COMMENT '用户ID',
    `action` VARCHAR(32) NOT NULL COMMENT '操作类型',
    `target_type` VARCHAR(32) DEFAULT NULL COMMENT '目标类型',
    `target_id` BIGINT UNSIGNED DEFAULT NULL COMMENT '目标ID',
    `target_name` VARCHAR(255) DEFAULT NULL COMMENT '目标名称',
    `details` JSON DEFAULT NULL COMMENT '操作详情',
    `ip_address` VARCHAR(45) NOT NULL COMMENT 'IP地址',
    `user_agent` VARCHAR(512) DEFAULT NULL COMMENT '客户端信息',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    KEY `idx_operation_logs_user_id` (`user_id`),
    KEY `idx_operation_logs_action` (`action`),
    KEY `idx_operation_logs_created_at` (`created_at`),
    KEY `idx_operation_logs_target` (`target_type`, `target_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='操作日志表';

-- 创建默认管理员用户 密码为 Admin123
INSERT INTO `users` (`username`, `email`, `password_hash`, `nickname`, `storage_quota`)
VALUES ('admin', 'admin@example.com', '$argon2id$v=19$m=65536,t=2,p=1$BjgpFYz8h/yjnJYjV497Tw$JCgRPDFvioq+FPQuR0i3a6kiTnLALv/F1A0eim7x7zE', '管理员', 107374182400);
