/**
 * @file UserDtos.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户资料与存储响应数据传输对象及 JSON 解析辅助函数
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>
#include <optional>

#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::models {

    // ==================== 用户资料 ====================

    /**
     * @brief 用户资料数据
     *
     * @details
     * 映射后端 UserProfileResponse。
     * 包含用户身份信息和基本存储统计。
     */
    struct UserProfileDto {
        quint64 id{};
        QString username;
        QString email;
        QString nickname;
        QString avatar;
        quint64 storageUsed{};
        quint64 storageQuota{};
        int fileCount{};
        int folderCount{};
        QString createdAt;
        QString updatedAt;
    };

    // ==================== 存储统计 ====================

    /**
     * @brief 单个存储分类条目
     */
    struct StorageCategoryDto {
        QString type; ///< "document"（文档）、"image"（图片）、"video"（视频）、"audio"（音频）、"other"（其他）
        quint64 size{};
        int count{};
    };

    /**
     * @brief 存储使用统计
     *
     * @details
     * 映射后端 StorageResponse。
     * 包含按分类的详细存储使用明细。
     */
    struct StorageDto {
        quint64 used{};
        quint64 quota{};
        double percentage{};
        int fileCount{};
        int folderCount{};
        QVector<StorageCategoryDto> categories;
    };

    // ==================== 更新资料 ====================

    /**
     * @brief 更新用户资料结果
     *
     * @details
     * 成功时返回更新后的资料数据。
     */
    struct UpdateProfileResultDto {
        QString nickname;
        QString avatar;
        QString updatedAt;
    };

    // ==================== 修改密码 ====================

    /**
     * @brief 修改密码结果
     *
     * @details
     * 简单的成功指示器；成功时不返回数据。
     */
    struct ChangePasswordResultDto {
        bool success{ false };
    };

    // ==================== JSON 解析辅助函数 ====================

    /**
     * @brief 从信封数据解析用户资料
     *
     * @details
     * 预期格式：data = { "user": { "id": N, "username": "...", ... } }
     */
    inline auto ParseUserProfile(const QJsonValue& dataVal) -> std::optional<UserProfileDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();
        const QJsonValue userVal = obj.value(QLatin1String("user"));
        if (!userVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject userObj = userVal.toObject();
        if (!userObj.contains(QLatin1String("id"))) {
            return std::nullopt;
        }

        UserProfileDto profile;
        profile.id = static_cast<quint64>(userObj.value(QLatin1String("id")).toDouble());
        profile.username = userObj.value(QLatin1String("username")).toString();
        profile.email = userObj.value(QLatin1String("email")).toString();
        profile.nickname = userObj.value(QLatin1String("nickname")).toString();
        profile.avatar = userObj.value(QLatin1String("avatar")).toString();
        profile.storageUsed = static_cast<quint64>(userObj.value(QLatin1String("storage_used")).toDouble());
        profile.storageQuota = static_cast<quint64>(userObj.value(QLatin1String("storage_quota")).toDouble());
        profile.fileCount = userObj.value(QLatin1String("file_count")).toInt();
        profile.folderCount = userObj.value(QLatin1String("folder_count")).toInt();
        profile.createdAt = userObj.value(QLatin1String("created_at")).toString();
        profile.updatedAt = userObj.value(QLatin1String("updated_at")).toString();

        return profile;
    }

    /**
     * @brief 从 JSON 对象解析单个存储分类
     */
    inline auto ParseStorageCategory(const QJsonObject& obj) -> StorageCategoryDto {
        StorageCategoryDto cat;
        cat.type = obj.value(QLatin1String("type")).toString();
        cat.size = static_cast<quint64>(obj.value(QLatin1String("size")).toDouble());
        cat.count = obj.value(QLatin1String("count")).toInt();
        return cat;
    }

    /**
     * @brief 从信封数据解析存储统计
     *
     * @details
     * 预期格式：data = { "storage": { "used": N, "quota": N, "percentage": F, ... } }
     */
    inline auto ParseStorage(const QJsonValue& dataVal) -> std::optional<StorageDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();
        const QJsonValue storageVal = obj.value(QLatin1String("storage"));
        if (!storageVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject storageObj = storageVal.toObject();

        StorageDto storage;
        storage.used = static_cast<quint64>(storageObj.value(QLatin1String("used")).toDouble());
        storage.quota = static_cast<quint64>(storageObj.value(QLatin1String("quota")).toDouble());
        storage.percentage = storageObj.value(QLatin1String("percentage")).toDouble();
        storage.fileCount = storageObj.value(QLatin1String("file_count")).toInt();
        storage.folderCount = storageObj.value(QLatin1String("folder_count")).toInt();

        const QJsonValue categoriesVal = storageObj.value(QLatin1String("categories"));
        if (categoriesVal.isArray()) {
            const QJsonArray categoriesArr = categoriesVal.toArray();
            storage.categories.reserve(categoriesArr.size());
            for (const auto& val : categoriesArr) {
                if (val.isObject()) {
                    storage.categories.append(ParseStorageCategory(val.toObject()));
                }
            }
        }

        return storage;
    }

    /**
     * @brief 从信封数据解析更新资料结果
     *
     * @details
     * 预期格式：data = { "nickname": "...", "avatar": "...", "updated_at": "..." }
     * 或：data = { "user": { "nickname": "...", "avatar": "...", "updated_at": "..." } }
     */
    inline auto ParseUpdateProfileResult(const QJsonValue& dataVal) -> std::optional<UpdateProfileResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();

        // 优先尝试直接字段
        if (obj.contains(QLatin1String("nickname")) || obj.contains(QLatin1String("updated_at"))) {
            UpdateProfileResultDto result;
            result.nickname = obj.value(QLatin1String("nickname")).toString();
            result.avatar = obj.value(QLatin1String("avatar")).toString();
            result.updatedAt = obj.value(QLatin1String("updated_at")).toString();
            return result;
        }

        // 尝试嵌套的 "user" 对象
        const QJsonValue userVal = obj.value(QLatin1String("user"));
        if (userVal.isObject()) {
            const QJsonObject userObj = userVal.toObject();
            UpdateProfileResultDto result;
            result.nickname = userObj.value(QLatin1String("nickname")).toString();
            result.avatar = userObj.value(QLatin1String("avatar")).toString();
            result.updatedAt = userObj.value(QLatin1String("updated_at")).toString();
            return result;
        }

        // 成功但无具体数据
        return UpdateProfileResultDto{};
    }

    /**
     * @brief 从信封数据解析修改密码结果
     *
     * @details
     * 预期格式：data = {}（成功时为空对象）
     */
    inline auto ParseChangePasswordResult(const QJsonValue& dataVal) -> std::optional<ChangePasswordResultDto> {
        // 任何非空数据表示成功
        ChangePasswordResultDto result;
        result.success = true;
        return result;
    }

} // namespace disk::qml::models
