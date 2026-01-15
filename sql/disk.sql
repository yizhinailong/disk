-- ============================================
-- 网盘系统存储过程集合
-- ============================================

DELIMITER $$

-- ============================================
-- 1. 文件上传完成处理（支持秒传）
-- ============================================
DROP PROCEDURE IF EXISTS sp_file_upload_complete$$
CREATE PROCEDURE sp_file_upload_complete(
    IN p_user_id BIGINT UNSIGNED,
    IN p_folder_id BIGINT UNSIGNED,
    IN p_filename VARCHAR(255),
    IN p_extension VARCHAR(32),
    IN p_size BIGINT UNSIGNED,
    IN p_mime_type VARCHAR(128),
    IN p_hash_md5 CHAR(32),
    IN p_hash_sha256 CHAR(64),
    IN p_storage_path VARCHAR(512),
    OUT p_file_id BIGINT UNSIGNED,
    OUT p_content_id BIGINT UNSIGNED,
    OUT p_is_instant_upload TINYINT,
    OUT p_result_code INT,
    OUT p_result_msg VARCHAR(255)
)
BEGIN
    DECLARE v_content_id BIGINT UNSIGNED;
    DECLARE v_file_path VARCHAR(4096);
    DECLARE v_parent_path VARCHAR(4096);
    DECLARE v_file_exists INT DEFAULT 0;
    
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_result_code = -1;
        SET p_result_msg = CONCAT('Error: ', SQLSTATE, ' - ', SQLERRM);
    END;
    
    START TRANSACTION;
    
    -- 检查文件内容是否已存在（秒传检测）
    SELECT id INTO v_content_id
    FROM file_contents
    WHERE hash_md5 = p_hash_md5 AND hash_sha256 = p_hash_sha256
    LIMIT 1;
    
    IF v_content_id IS NULL THEN
        -- 文件内容不存在，创建新记录
        INSERT INTO file_contents (hash_md5, hash_sha256, size, storage_path, mime_type, ref_count)
        VALUES (p_hash_md5, p_hash_sha256, p_size, p_storage_path, p_mime_type, 1);
        SET v_content_id = LAST_INSERT_ID();
        SET p_is_instant_upload = 0;
    ELSE
        -- 文件内容已存在，增加引用计数
        UPDATE file_contents SET ref_count = ref_count + 1 WHERE id = v_content_id;
        SET p_is_instant_upload = 1;
    END IF;
    
    -- 检查同目录下是否存在同名文件
    SELECT COUNT(*) INTO v_file_exists
    FROM files
    WHERE user_id = p_user_id AND folder_id = p_folder_id AND name = p_filename;
    
    IF v_file_exists > 0 THEN
        ROLLBACK;
        SET p_result_code = -2;
        SET p_result_msg = 'File with same name already exists';
        LEAVE sp;
    END IF;
    
    -- 获取父文件夹路径
    IF p_folder_id = 0 THEN
        SET v_parent_path = '/';
    ELSE
        SELECT path INTO v_parent_path FROM folders WHERE id = p_folder_id AND user_id = p_user_id;
        IF v_parent_path IS NULL THEN
            ROLLBACK;
            SET p_result_code = -3;
            SET p_result_msg = 'Parent folder not found';
            LEAVE sp;
        END IF;
    END IF;
    
    -- 构建文件完整路径
    SET v_file_path = CONCAT(v_parent_path, p_filename);
    
    -- 创建文件记录
    INSERT INTO files (user_id, content_id, folder_id, name, extension, size, mime_type, path)
    VALUES (p_user_id, v_content_id, p_folder_id, p_filename, p_extension, p_size, p_mime_type, v_file_path);
    SET p_file_id = LAST_INSERT_ID();
    
    -- 更新用户存储空间
    UPDATE users SET storage_used = storage_used + p_size WHERE id = p_user_id;
    
    -- 更新文件夹子项计数
    IF p_folder_id > 0 THEN
        UPDATE folders SET item_count = item_count + 1 WHERE id = p_folder_id;
    END IF;
    
    SET p_content_id = v_content_id;
    SET p_result_code = 0;
    SET p_result_msg = 'Success';
    
    COMMIT;
END$$

-- ============================================
-- 2. 文件/文件夹删除到回收站
-- ============================================
DROP PROCEDURE IF EXISTS sp_file_move_to_trash$$
CREATE PROCEDURE sp_file_move_to_trash(
    IN p_user_id BIGINT UNSIGNED,
    IN p_item_type ENUM('file', 'folder'),
    IN p_item_id BIGINT UNSIGNED,
    IN p_expires_days INT DEFAULT 30,
    OUT p_result_code INT,
    OUT p_result_msg VARCHAR(255)
)
BEGIN
    DECLARE v_item_name VARCHAR(255);
    DECLARE v_item_size BIGINT UNSIGNED DEFAULT 0;
    DECLARE v_original_folder_id BIGINT UNSIGNED;
    DECLARE v_original_path VARCHAR(4096);
    DECLARE v_content_id BIGINT UNSIGNED;
    DECLARE v_item_data JSON;
    DECLARE v_expires_at DATETIME;
    
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_result_code = -1;
        SET p_result_msg = CONCAT('Error: ', SQLSTATE, ' - ', SQLERRM);
    END;
    
    START TRANSACTION;
    
    SET v_expires_at = DATE_ADD(NOW(), INTERVAL p_expires_days DAY);
    
    IF p_item_type = 'file' THEN
        -- 处理文件
        SELECT name, size, folder_id, path, content_id, 
               JSON_OBJECT(
                   'id', id,
                   'user_id', user_id,
                   'content_id', content_id,
                   'folder_id', folder_id,
                   'name', name,
                   'extension', extension,
                   'size', size,
                   'mime_type', mime_type,
                   'path', path,
                   'is_favorite', is_favorite,
                   'download_count', download_count,
                   'created_at', created_at,
                   'updated_at', updated_at
               ) INTO v_item_name, v_item_size, v_original_folder_id, v_original_path, v_content_id, v_item_data
        FROM files
        WHERE id = p_item_id AND user_id = p_user_id;
        
        IF v_item_name IS NULL THEN
            ROLLBACK;
            SET p_result_code = -2;
            SET p_result_msg = 'File not found';
            LEAVE sp;
        END IF;
        
        -- 插入回收站
        INSERT INTO trash (user_id, item_type, item_id, item_name, item_size, original_folder_id, original_path, item_data, expires_at)
        VALUES (p_user_id, 'file', p_item_id, v_item_name, v_item_size, v_original_folder_id, v_original_path, v_item_data, v_expires_at);
        
        -- 删除文件记录
        DELETE FROM files WHERE id = p_item_id;
        
        -- 减少文件内容引用计数
        UPDATE file_contents SET ref_count = ref_count - 1 WHERE id = v_content_id;
        
        -- 更新用户存储空间
        UPDATE users SET storage_used = storage_used - v_item_size WHERE id = p_user_id;
        
        -- 更新文件夹子项计数
        IF v_original_folder_id > 0 THEN
            UPDATE folders SET item_count = item_count - 1 WHERE id = v_original_folder_id;
        END IF;
        
    ELSEIF p_item_type = 'folder' THEN
        -- 处理文件夹（递归删除子项）
        SELECT name, folder_id, path,
               JSON_OBJECT(
                   'id', id,
                   'user_id', user_id,
                   'parent_id', parent_id,
                   'name', name,
                   'path', path,
                   'depth', depth,
                   'item_count', item_count,
                   'created_at', created_at,
                   'updated_at', updated_at
               ) INTO v_item_name, v_original_folder_id, v_original_path, v_item_data
        FROM folders
        WHERE id = p_item_id AND user_id = p_user_id;
        
        IF v_item_name IS NULL THEN
            ROLLBACK;
            SET p_result_code = -2;
            SET p_result_msg = 'Folder not found';
            LEAVE sp;
        END IF;
        
        -- 计算文件夹总大小（递归计算所有文件）
        SELECT COALESCE(SUM(size), 0) INTO v_item_size
        FROM files
        WHERE user_id = p_user_id AND path LIKE CONCAT(v_original_path, '%');
        
        -- 插入回收站
        INSERT INTO trash (user_id, item_type, item_id, item_name, item_size, original_folder_id, original_path, item_data, expires_at)
        VALUES (p_user_id, 'folder', p_item_id, v_item_name, v_item_size, v_original_folder_id, v_original_path, v_item_data, v_expires_at);
        
        -- 删除文件夹及其所有子项（外键级联删除）
        DELETE FROM folders WHERE id = p_item_id;
        
        -- 更新用户存储空间
        UPDATE users SET storage_used = storage_used - v_item_size WHERE id = p_user_id;
        
        -- 更新父文件夹子项计数
        IF v_original_folder_id > 0 THEN
            UPDATE folders SET item_count = item_count - 1 WHERE id = v_original_folder_id;
        END IF;
    END IF;
    
    SET p_result_code = 0;
    SET p_result_msg = 'Success';
    
    COMMIT;
END$$

-- ============================================
-- 3. 从回收站恢复文件/文件夹
-- ============================================
DROP PROCEDURE IF EXISTS sp_file_restore_from_trash$$
CREATE PROCEDURE sp_file_restore_from_trash(
    IN p_user_id BIGINT UNSIGNED,
    IN p_trash_id BIGINT UNSIGNED,
    OUT p_result_code INT,
    OUT p_result_msg VARCHAR(255)
)
BEGIN
    DECLARE v_item_type ENUM('file', 'folder');
    DECLARE v_item_id BIGINT UNSIGNED;
    DECLARE v_item_data JSON;
    DECLARE v_original_folder_id BIGINT UNSIGNED;
    DECLARE v_item_size BIGINT UNSIGNED;
    DECLARE v_content_id BIGINT UNSIGNED;
    
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_result_code = -1;
        SET p_result_msg = CONCAT('Error: ', SQLSTATE, ' - ', SQLERRM);
    END;
    
    START TRANSACTION;
    
    -- 获取回收站记录
    SELECT item_type, item_id, item_data, original_folder_id, item_size
    INTO v_item_type, v_item_id, v_item_data, v_original_folder_id, v_item_size
    FROM trash
    WHERE id = p_trash_id AND user_id = p_user_id;
    
    IF v_item_type IS NULL THEN
        ROLLBACK;
        SET p_result_code = -2;
        SET p_result_msg = 'Trash item not found';
        LEAVE sp;
    END IF;
    
    IF v_item_type = 'file' THEN
        -- 恢复文件
        INSERT INTO files (
            user_id, content_id, folder_id, name, extension, size, mime_type, path,
            is_favorite, download_count, created_at, updated_at
        )
        SELECT 
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.user_id')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.content_id')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.folder_id')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.name')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.extension')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.size')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.mime_type')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.path')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.is_favorite')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.download_count')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.created_at')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.updated_at'))
        WHERE NOT EXISTS (
            SELECT 1 FROM files 
            WHERE user_id = JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.user_id'))
            AND folder_id = JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.folder_id'))
            AND name = JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.name'))
        );
        
        SET v_content_id = JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.content_id'));
        
        -- 增加文件内容引用计数
        UPDATE file_contents SET ref_count = ref_count + 1 WHERE id = v_content_id;
        
        -- 更新用户存储空间
        UPDATE users SET storage_used = storage_used + v_item_size WHERE id = p_user_id;
        
        -- 更新文件夹子项计数
        IF v_original_folder_id > 0 THEN
            UPDATE folders SET item_count = item_count + 1 WHERE id = v_original_folder_id;
        END IF;
        
    ELSEIF v_item_type = 'folder' THEN
        -- 恢复文件夹（需要递归恢复子项，这里简化处理）
        -- 注意：实际实现中可能需要更复杂的逻辑来恢复整个文件夹树
        INSERT INTO folders (
            user_id, parent_id, name, path, depth, item_count, created_at, updated_at
        )
        SELECT 
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.user_id')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.parent_id')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.name')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.path')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.depth')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.item_count')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.created_at')),
            JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.updated_at'))
        WHERE NOT EXISTS (
            SELECT 1 FROM folders 
            WHERE user_id = JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.user_id'))
            AND parent_id = JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.parent_id'))
            AND name = JSON_UNQUOTE(JSON_EXTRACT(v_item_data, '$.name'))
        );
        
        -- 更新用户存储空间
        UPDATE users SET storage_used = storage_used + v_item_size WHERE id = p_user_id;
        
        -- 更新父文件夹子项计数
        IF v_original_folder_id > 0 THEN
            UPDATE folders SET item_count = item_count + 1 WHERE id = v_original_folder_id;
        END IF;
    END IF;
    
    -- 删除回收站记录
    DELETE FROM trash WHERE id = p_trash_id;
    
    SET p_result_code = 0;
    SET p_result_msg = 'Success';
    
    COMMIT;
END$$

-- ============================================
-- 4. 彻底删除回收站项目
-- ============================================
DROP PROCEDURE IF EXISTS sp_file_delete_permanently$$
CREATE PROCEDURE sp_file_delete_permanently(
    IN p_user_id BIGINT UNSIGNED,
    IN p_trash_id BIGINT UNSIGNED,
    OUT p_result_code INT,
    OUT p_result_msg VARCHAR(255)
)
BEGIN
    DECLARE v_item_type ENUM('file', 'folder');
    DECLARE v_content_id BIGINT UNSIGNED;
    DECLARE v_ref_count INT UNSIGNED;
    
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_result_code = -1;
        SET p_result_msg = CONCAT('Error: ', SQLSTATE, ' - ', SQLERRM);
    END;
    
    START TRANSACTION;
    
    -- 获取回收站记录类型
    SELECT item_type, 
           CASE item_type 
               WHEN 'file' THEN JSON_UNQUOTE(JSON_EXTRACT(item_data, '$.content_id'))
               ELSE NULL
           END
    INTO v_item_type, v_content_id
    FROM trash
    WHERE id = p_trash_id AND user_id = p_user_id;
    
    IF v_item_type IS NULL THEN
        ROLLBACK;
        SET p_result_code = -2;
        SET p_result_msg = 'Trash item not found';
        LEAVE sp;
    END IF;
    
    -- 如果是文件，检查并清理文件内容
    IF v_item_type = 'file' AND v_content_id IS NOT NULL THEN
        SELECT ref_count INTO v_ref_count FROM file_contents WHERE id = v_content_id;
        
        IF v_ref_count = 0 THEN
            -- 引用计数为0，可以删除文件内容（实际删除物理文件需要应用层处理）
            DELETE FROM file_contents WHERE id = v_content_id;
        END IF;
    END IF;
    
    -- 删除回收站记录
    DELETE FROM trash WHERE id = p_trash_id;
    
    SET p_result_code = 0;
    SET p_result_msg = 'Success';
    
    COMMIT;
END$$

-- ============================================
-- 5. 文件移动
-- ============================================
DROP PROCEDURE IF EXISTS sp_file_move$$
CREATE PROCEDURE sp_file_move(
    IN p_user_id BIGINT UNSIGNED,
    IN p_item_type ENUM('file', 'folder'),
    IN p_item_id BIGINT UNSIGNED,
    IN p_target_folder_id BIGINT UNSIGNED,
    OUT p_result_code INT,
    OUT p_result_msg VARCHAR(255)
)
BEGIN
    DECLARE v_current_folder_id BIGINT UNSIGNED;
    DECLARE v_name VARCHAR(255);
    DECLARE v_target_path VARCHAR(4096);
    DECLARE v_current_path VARCHAR(4096);
    DECLARE v_name_exists INT DEFAULT 0;
    
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_result_code = -1;
        SET p_result_msg = CONCAT('Error: ', SQLSTATE, ' - ', SQLERRM);
    END;
    
    START TRANSACTION;
    
    -- 获取目标文件夹路径
    IF p_target_folder_id = 0 THEN
        SET v_target_path = '/';
    ELSE
        SELECT path INTO v_target_path 
        FROM folders 
        WHERE id = p_target_folder_id AND user_id = p_user_id;
        
        IF v_target_path IS NULL THEN
            ROLLBACK;
            SET p_result_code = -2;
            SET p_result_msg = 'Target folder not found';
            LEAVE sp;
        END IF;
    END IF;
    
    IF p_item_type = 'file' THEN
        -- 移动文件
        SELECT folder_id, name, path 
        INTO v_current_folder_id, v_name, v_current_path
        FROM files 
        WHERE id = p_item_id AND user_id = p_user_id;
        
        IF v_name IS NULL THEN
            ROLLBACK;
            SET p_result_code = -3;
            SET p_result_msg = 'File not found';
            LEAVE sp;
        END IF;
        
        -- 检查目标位置是否存在同名文件
        SELECT COUNT(*) INTO v_name_exists
        FROM files
        WHERE user_id = p_user_id AND folder_id = p_target_folder_id AND name = v_name;
        
        IF v_name_exists > 0 THEN
            ROLLBACK;
            SET p_result_code = -4;
            SET p_result_msg = 'File with same name already exists in target folder';
            LEAVE sp;
        END IF;
        
        -- 更新文件位置
        UPDATE files 
        SET folder_id = p_target_folder_id,
            path = CONCAT(v_target_path, v_name)
        WHERE id = p_item_id;
        
        -- 更新文件夹子项计数
        IF v_current_folder_id > 0 THEN
            UPDATE folders SET item_count = item_count - 1 WHERE id = v_current_folder_id;
        END IF;
        IF p_target_folder_id > 0 THEN
            UPDATE folders SET item_count = item_count + 1 WHERE id = p_target_folder_id;
        END IF;
        
    ELSEIF p_item_type = 'folder' THEN
        -- 移动文件夹
        SELECT parent_id, name, path
        INTO v_current_folder_id, v_name, v_current_path
        FROM folders
        WHERE id = p_item_id AND user_id = p_user_id;
        
        IF v_name IS NULL THEN
            ROLLBACK;
            SET p_result_code = -3;
            SET p_result_msg = 'Folder not found';
            LEAVE sp;
        END IF;
        
        -- 检查目标位置是否存在同名文件夹
        SELECT COUNT(*) INTO v_name_exists
        FROM folders
        WHERE user_id = p_user_id AND parent_id = p_target_folder_id AND name = v_name;
        
        IF v_name_exists > 0 THEN
            ROLLBACK;
            SET p_result_code = -4;
            SET p_result_msg = 'Folder with same name already exists in target folder';
            LEAVE sp;
        END IF;
        
        -- 更新文件夹位置和路径（需要递归更新子文件夹路径）
        UPDATE folders
        SET parent_id = p_target_folder_id,
            path = CONCAT(v_target_path, v_name),
            depth = (SELECT depth FROM folders WHERE id = p_target_folder_id) + 1
        WHERE id = p_item_id;
        
        -- 递归更新子文件夹路径
        UPDATE folders
        SET path = REPLACE(path, v_current_path, CONCAT(v_target_path, v_name))
        WHERE user_id = p_user_id AND path LIKE CONCAT(v_current_path, '%') AND id != p_item_id;
        
        -- 更新文件夹子项计数
        IF v_current_folder_id > 0 THEN
            UPDATE folders SET item_count = item_count - 1 WHERE id = v_current_folder_id;
        END IF;
        IF p_target_folder_id > 0 THEN
            UPDATE folders SET item_count = item_count + 1 WHERE id = p_target_folder_id;
        END IF;
    END IF;
    
    SET p_result_code = 0;
    SET p_result_msg = 'Success';
    
    COMMIT;
END$$

-- ============================================
-- 6. 文件重命名
-- ============================================
DROP PROCEDURE IF EXISTS sp_file_rename$$
CREATE PROCEDURE sp_file_rename(
    IN p_user_id BIGINT UNSIGNED,
    IN p_item_type ENUM('file', 'folder'),
    IN p_item_id BIGINT UNSIGNED,
    IN p_new_name VARCHAR(255),
    OUT p_result_code INT,
    OUT p_result_msg VARCHAR(255)
)
BEGIN
    DECLARE v_folder_id BIGINT UNSIGNED;
    DECLARE v_parent_id BIGINT UNSIGNED;
    DECLARE v_old_name VARCHAR(255);
    DECLARE v_old_path VARCHAR(4096);
    DECLARE v_parent_path VARCHAR(4096);
    DECLARE v_name_exists INT DEFAULT 0;
    
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_result_code = -1;
        SET p_result_msg = CONCAT('Error: ', SQLSTATE, ' - ', SQLERRM);
    END;
    
    START TRANSACTION;
    
    IF p_item_type = 'file' THEN
        SELECT folder_id, name, path
        INTO v_folder_id, v_old_name, v_old_path
        FROM files
        WHERE id = p_item_id AND user_id = p_user_id;
        
        IF v_old_name IS NULL THEN
            ROLLBACK;
            SET p_result_code = -2;
            SET p_result_msg = 'File not found';
            LEAVE sp;
        END IF;
        
        -- 检查同目录下是否存在同名文件
        SELECT COUNT(*) INTO v_name_exists
        FROM files
        WHERE user_id = p_user_id AND folder_id = v_folder_id AND name = p_new_name AND id != p_item_id;
        
        IF v_name_exists > 0 THEN
            ROLLBACK;
            SET p_result_code = -3;
            SET p_result_msg = 'File with same name already exists';
            LEAVE sp;
        END IF;
        
        -- 获取父路径
        IF v_folder_id = 0 THEN
            SET v_parent_path = '/';
        ELSE
            SELECT path INTO v_parent_path FROM folders WHERE id = v_folder_id;
        END IF;
        
        -- 更新文件名和路径
        UPDATE files
        SET name = p_new_name,
            path = CONCAT(v_parent_path, p_new_name)
        WHERE id = p_item_id;
        
    ELSEIF p_item_type = 'folder' THEN
        SELECT parent_id, name, path
        INTO v_parent_id, v_old_name, v_old_path
        FROM folders
        WHERE id = p_item_id AND user_id = p_user_id;
        
        IF v_old_name IS NULL THEN
            ROLLBACK;
            SET p_result_code = -2;
            SET p_result_msg = 'Folder not found';
            LEAVE sp;
        END IF;
        
        -- 检查同目录下是否存在同名文件夹
        SELECT COUNT(*) INTO v_name_exists
        FROM folders
        WHERE user_id = p_user_id AND parent_id = v_parent_id AND name = p_new_name AND id != p_item_id;
        
        IF v_name_exists > 0 THEN
            ROLLBACK;
            SET p_result_code = -3;
            SET p_result_msg = 'Folder with same name already exists';
            LEAVE sp;
        END IF;
        
        -- 获取父路径
        IF v_parent_id = 0 THEN
            SET v_parent_path = '/';
        ELSE
            SELECT path INTO v_parent_path FROM folders WHERE id = v_parent_id;
        END IF;
        
        -- 更新文件夹名和路径
        UPDATE folders
        SET name = p_new_name,
            path = CONCAT(v_parent_path, p_new_name)
        WHERE id = p_item_id;
        
        -- 递归更新子文件夹和文件的路径
        UPDATE folders
        SET path = REPLACE(path, v_old_path, CONCAT(v_parent_path, p_new_name))
        WHERE user_id = p_user_id AND path LIKE CONCAT(v_old_path, '%') AND id != p_item_id;
        
        UPDATE files
        SET path = REPLACE(path, v_old_path, CONCAT(v_parent_path, p_new_name))
        WHERE user_id = p_user_id AND path LIKE CONCAT(v_old_path, '%');
    END IF;
    
    SET p_result_code = 0;
    SET p_result_msg = 'Success';
    
    COMMIT;
END$$

-- ============================================
-- 7. 文件复制
-- ============================================
DROP PROCEDURE IF EXISTS sp_file_copy$$
CREATE PROCEDURE sp_file_copy(
    IN p_user_id BIGINT UNSIGNED,
    IN p_file_id BIGINT UNSIGNED,
    IN p_target_folder_id BIGINT UNSIGNED,
    IN p_new_name VARCHAR(255),
    OUT p_new_file_id BIGINT UNSIGNED,
    OUT p_result_code INT,
    OUT p_result_msg VARCHAR(255)
)
BEGIN
    DECLARE v_content_id BIGINT UNSIGNED;
    DECLARE v_name VARCHAR(255);
    DECLARE v_extension VARCHAR(32);
    DECLARE v_size BIGINT UNSIGNED;
    DECLARE v_mime_type VARCHAR(128);
    DECLARE v_target_path VARCHAR(4096);
    DECLARE v_name_exists INT DEFAULT 0;
    
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_result_code = -1;
        SET p_result_msg = CONCAT('Error: ', SQLSTATE, ' - ', SQLERRM);
    END;
    
    START TRANSACTION;
    
    -- 获取源文件信息
    SELECT content_id, name, extension, size, mime_type
    INTO v_content_id, v_name, v_extension, v_size, v_mime_type
    FROM files
    WHERE id = p_file_id AND user_id = p_user_id;
    
    IF v_name IS NULL THEN
        ROLLBACK;
        SET p_result_code = -2;
        SET p_result_msg = 'Source file not found';
        LEAVE sp;
    END IF;
    
    -- 如果没有指定新名称，使用原名称
    IF p_new_name IS NULL OR p_new_name = '' THEN
        SET p_new_name = v_name;
    END IF;
    
    -- 获取目标路径
    IF p_target_folder_id = 0 THEN
        SET v_target_path = '/';
    ELSE
        SELECT path INTO v_target_path 
        FROM folders 
        WHERE id = p_target_folder_id AND user_id = p_user_id;
        
        IF v_target_path IS NULL THEN
            ROLLBACK;
            SET p_result_code = -3;
            SET p_result_msg = 'Target folder not found';
            LEAVE sp;
        END IF;
    END IF;
    
    -- 检查目标位置是否存在同名文件
    SELECT COUNT(*) INTO v_name_exists
    FROM files
    WHERE user_id = p_user_id AND folder_id = p_target_folder_id AND name = p_new_name;
    
    IF v_name_exists > 0 THEN
        ROLLBACK;
        SET p_result_code = -4;
        SET p_result_msg = 'File with same name already exists in target folder';
        LEAVE sp;
    END IF;
    
    -- 创建新文件记录
    INSERT INTO files (user_id, content_id, folder_id, name, extension, size, mime_type, path)
    VALUES (p_user_id, v_content_id, p_target_folder_id, p_new_name, v_extension, v_size, v_mime_type, CONCAT(v_target_path, p_new_name));
    SET p_new_file_id = LAST_INSERT_ID();
    
    -- 增加文件内容引用计数
    UPDATE file_contents SET ref_count = ref_count + 1 WHERE id = v_content_id;
    
    -- 注意：复制文件不增加用户存储空间（因为共享同一份物理文件）
    -- 但可以更新文件夹子项计数
    IF p_target_folder_id > 0 THEN
        UPDATE folders SET item_count = item_count + 1 WHERE id = p_target_folder_id;
    END IF;
    
    SET p_result_code = 0;
    SET p_result_msg = 'Success';
    
    COMMIT;
END$$

-- ============================================
-- 8. 重新计算用户存储空间
-- ============================================
DROP PROCEDURE IF EXISTS sp_recalculate_user_storage$$
CREATE PROCEDURE sp_recalculate_user_storage(
    IN p_user_id BIGINT UNSIGNED,
    OUT p_result_code INT,
    OUT p_result_msg VARCHAR(255)
)
BEGIN
    DECLARE v_total_size BIGINT UNSIGNED DEFAULT 0;
    
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_result_code = -1;
        SET p_result_msg = CONCAT('Error: ', SQLSTATE, ' - ', SQLERRM);
    END;
    
    START TRANSACTION;
    
    -- 计算用户所有文件的总大小
    SELECT COALESCE(SUM(size), 0) INTO v_total_size
    FROM files
    WHERE user_id = p_user_id;
    
    -- 更新用户存储空间
    UPDATE users SET storage_used = v_total_size WHERE id = p_user_id;
    
    SET p_result_code = 0;
    SET p_result_msg = CONCAT('Recalculated storage: ', v_total_size, ' bytes');
    
    COMMIT;
END$$

-- ============================================
-- 9. 更新文件夹子项计数
-- ============================================
DROP PROCEDURE IF EXISTS sp_update_folder_item_count$$
CREATE PROCEDURE sp_update_folder_item_count(
    IN p_folder_id BIGINT UNSIGNED,
    OUT p_result_code INT,
    OUT p_result_msg VARCHAR(255)
)
BEGIN
    DECLARE v_item_count INT UNSIGNED DEFAULT 0;
    
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_result_code = -1;
        SET p_result_msg = CONCAT('Error: ', SQLSTATE, ' - ', SQLERRM);
    END;
    
    START TRANSACTION;
    
    -- 计算文件夹下的文件和子文件夹数量
    SELECT 
        (SELECT COUNT(*) FROM files WHERE folder_id = p_folder_id) +
        (SELECT COUNT(*) FROM folders WHERE parent_id = p_folder_id)
    INTO v_item_count;
    
    -- 更新文件夹子项计数
    UPDATE folders SET item_count = v_item_count WHERE id = p_folder_id;
    
    SET p_result_code = 0;
    SET p_result_msg = CONCAT('Updated item count: ', v_item_count);
    
    COMMIT;
END$$

-- ============================================
-- 10. 创建分享
-- ============================================
DROP PROCEDURE IF EXISTS sp_create_share$$
CREATE PROCEDURE sp_create_share(
    IN p_user_id BIGINT UNSIGNED,
    IN p_share_code VARCHAR(32),
    IN p_password_hash VARCHAR(255),
    IN p_permission ENUM('view', 'download'),
    IN p_expires_at DATETIME,
    IN p_item_ids TEXT,  -- JSON数组，格式: [{"type":"file","id":1},{"type":"folder","id":2}]
    OUT p_share_id BIGINT UNSIGNED,
    OUT p_result_code INT,
    OUT p_result_msg VARCHAR(255)
)
BEGIN
    DECLARE v_item_count INT DEFAULT 0;
    DECLARE v_item_type VARCHAR(10);
    DECLARE v_item_id BIGINT UNSIGNED;
    DECLARE v_done INT DEFAULT 0;
    DECLARE v_json JSON;
    
    DECLARE cur_items CURSOR FOR 
        SELECT JSON_UNQUOTE(JSON_EXTRACT(p_item_ids, CONCAT('$[', idx, '].type'))),
               JSON_UNQUOTE(JSON_EXTRACT(p_item_ids, CONCAT('$[', idx, '].id')))
        FROM (SELECT 0 as idx UNION SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 
              UNION SELECT 5 UNION SELECT 6 UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) t
        WHERE JSON_EXTRACT(p_item_ids, CONCAT('$[', idx, ']')) IS NOT NULL;
    
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_done = 1;
    
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_result_code = -1;
        SET p_result_msg = CONCAT('Error: ', SQLSTATE, ' - ', SQLERRM);
    END;
    
    START TRANSACTION;
    
    -- 检查分享码是否已存在
    SELECT COUNT(*) INTO v_item_count
    FROM shares
    WHERE share_code = p_share_code;
    
    IF v_item_count > 0 THEN
        ROLLBACK;
        SET p_result_code = -2;
        SET p_result_msg = 'Share code already exists';
        LEAVE sp;
    END IF;
    
    -- 创建分享记录
    INSERT INTO shares (share_code, user_id, password_hash, permission, expires_at)
    VALUES (p_share_code, p_user_id, p_password_hash, p_permission, p_expires_at);
    SET p_share_id = LAST_INSERT_ID();
    
    -- 解析并插入分享文件关联（简化版本，实际需要更复杂的JSON解析）
    SET v_json = CAST(p_item_ids AS JSON);
    SET v_item_count = JSON_LENGTH(v_json);
    
    -- 这里简化处理，实际应该使用循环解析JSON数组
    -- 建议在应用层解析JSON后，多次调用插入操作
    
    SET p_result_code = 0;
    SET p_result_msg = 'Success';
    
    COMMIT;
END$$

-- ============================================
-- 11. 批量清理过期数据
-- ============================================
DROP PROCEDURE IF EXISTS sp_cleanup_expired_data$$
CREATE PROCEDURE sp_cleanup_expired_data(
    IN p_batch_size INT DEFAULT 1000,
    OUT p_deleted_count INT,
    OUT p_result_code INT,
    OUT p_result_msg VARCHAR(255)
)
BEGIN
    DECLARE v_deleted_upload INT DEFAULT 0;
    DECLARE v_deleted_trash INT DEFAULT 0;
    DECLARE v_deleted_shares INT DEFAULT 0;
    DECLARE v_deleted_contents INT DEFAULT 0;
    
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_result_code = -1;
        SET p_result_msg = CONCAT('Error: ', SQLSTATE, ' - ', SQLERRM);
    END;
    
    START TRANSACTION;
    
    -- 清理过期的上传任务
    DELETE FROM upload_tasks 
    WHERE (status = 3 OR expires_at < NOW())
    LIMIT p_batch_size;
    SET v_deleted_upload = ROW_COUNT();
    
    -- 清理过期的回收站记录
    DELETE FROM trash 
    WHERE expires_at < NOW()
    LIMIT p_batch_size;
    SET v_deleted_trash = ROW_COUNT();
    
    -- 清理过期的分享
    UPDATE shares 
    SET status = 2 
    WHERE expires_at < NOW() AND status = 1;
    SET v_deleted_shares = ROW_COUNT();
    
    -- 清理无引用的文件内容（注意：实际删除物理文件需要应用层处理）
    DELETE FROM file_contents 
    WHERE ref_count = 0
    LIMIT p_batch_size;
    SET v_deleted_contents = ROW_COUNT();
    
    SET p_deleted_count = v_deleted_upload + v_deleted_trash + v_deleted_shares + v_deleted_contents;
    SET p_result_code = 0;
    SET p_result_msg = CONCAT('Deleted: upload=', v_deleted_upload, 
                             ', trash=', v_deleted_trash,
                             ', shares=', v_deleted_shares,
                             ', contents=', v_deleted_contents);
    
    COMMIT;
END$$

-- ============================================
-- 12. 获取用户文件列表（查询存储过程）
-- ============================================
DROP PROCEDURE IF EXISTS sp_get_file_list$$
CREATE PROCEDURE sp_get_file_list(
    IN p_user_id BIGINT UNSIGNED,
    IN p_folder_id BIGINT UNSIGNED,
    IN p_page_size INT DEFAULT 20,
    IN p_page_offset INT DEFAULT 0
)
BEGIN
    SELECT 
        f.id,
        f.user_id,
        f.content_id,
        f.folder_id,
        f.name,
        f.extension,
        f.size,
        f.mime_type,
        f.path,
        f.is_favorite,
        f.download_count,
        f.last_accessed_at,
        f.created_at,
        f.updated_at,
        fc.hash_md5,
        fc.hash_sha256,
        fc.storage_path
    FROM files f
    JOIN file_contents fc ON f.content_id = fc.id
    WHERE f.user_id = p_user_id AND f.folder_id = p_folder_id
    ORDER BY f.name ASC
    LIMIT p_page_size OFFSET p_page_offset;
END$$

-- ============================================
-- 13. 秒传检测（查询存储过程）
-- ============================================
DROP PROCEDURE IF EXISTS sp_check_instant_upload$$
CREATE PROCEDURE sp_check_instant_upload(
    IN p_hash_md5 CHAR(32),
    IN p_hash_sha256 CHAR(64)
)
BEGIN
    SELECT 
        id,
        storage_path,
        size,
        mime_type
    FROM file_contents
    WHERE hash_md5 = p_hash_md5 AND hash_sha256 = p_hash_sha256
    LIMIT 1;
END$$

-- ============================================
-- 14. 获取目录树（查询存储过程）
-- ============================================
DROP PROCEDURE IF EXISTS sp_get_folder_tree$$
CREATE PROCEDURE sp_get_folder_tree(
    IN p_user_id BIGINT UNSIGNED,
    IN p_root_folder_id BIGINT UNSIGNED DEFAULT 0,
    IN p_max_depth INT DEFAULT 10
)
BEGIN
    WITH RECURSIVE folder_tree AS (
        SELECT 
            id, name, parent_id, path, depth, item_count, created_at, updated_at, 0 as level
        FROM folders
        WHERE user_id = p_user_id AND parent_id = p_root_folder_id
        
        UNION ALL
        
        SELECT 
            f.id, f.name, f.parent_id, f.path, f.depth, f.item_count, f.created_at, f.updated_at, ft.level + 1
        FROM folders f
        INNER JOIN folder_tree ft ON f.parent_id = ft.id
        WHERE f.user_id = p_user_id AND ft.level < p_max_depth
    )
    SELECT * FROM folder_tree ORDER BY path;
END$$

DELIMITER ;