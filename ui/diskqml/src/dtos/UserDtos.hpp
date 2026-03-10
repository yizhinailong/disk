/**
 * @file UserDtos.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief User profile & storage response DTOs and JSON parsing helpers for the QML client
 * @version 0.1
 * @date 2026-03-10
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

    // ==================== User Profile ====================

    /**
     * @brief User profile data.
     *
     * @details
     * Mirrors backend UserProfileResponse.
     * Contains user identity and basic storage statistics.
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

    // ==================== Storage Statistics ====================

    /**
     * @brief A single storage category entry.
     */
    struct StorageCategoryDto {
        QString type; ///< "document", "image", "video", "audio", "other"
        quint64 size{};
        int count{};
    };

    /**
     * @brief Storage usage statistics.
     *
     * @details
     * Mirrors backend StorageResponse.
     * Contains detailed storage usage breakdown by category.
     */
    struct StorageDto {
        quint64 used{};
        quint64 quota{};
        double percentage{};
        int fileCount{};
        int folderCount{};
        QVector<StorageCategoryDto> categories;
    };

    // ==================== Update Profile ====================

    /**
     * @brief Result of updating user profile.
     *
     * @details
     * Returns the updated profile data on success.
     */
    struct UpdateProfileResultDto {
        QString nickname;
        QString avatar;
        QString updatedAt;
    };

    // ==================== Change Password ====================

    /**
     * @brief Result of changing password.
     *
     * @details
     * Simple success indicator; no data returned on success.
     */
    struct ChangePasswordResultDto {
        bool success{ false };
    };

    // ==================== JSON Parsing Helpers ====================

    /**
     * @brief Parse user profile from envelope data.
     *
     * @details
     * Expected shape: data = { "user": { "id": N, "username": "...", ... } }
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
     * @brief Parse a single storage category from JSON object.
     */
    inline auto ParseStorageCategory(const QJsonObject& obj) -> StorageCategoryDto {
        StorageCategoryDto cat;
        cat.type = obj.value(QLatin1String("type")).toString();
        cat.size = static_cast<quint64>(obj.value(QLatin1String("size")).toDouble());
        cat.count = obj.value(QLatin1String("count")).toInt();
        return cat;
    }

    /**
     * @brief Parse storage statistics from envelope data.
     *
     * @details
     * Expected shape: data = { "storage": { "used": N, "quota": N, "percentage": F, ... } }
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
     * @brief Parse update profile result from envelope data.
     *
     * @details
     * Expected shape: data = { "nickname": "...", "avatar": "...", "updated_at": "..." }
     * Or: data = { "user": { "nickname": "...", "avatar": "...", "updated_at": "..." } }
     */
    inline auto ParseUpdateProfileResult(const QJsonValue& dataVal) -> std::optional<UpdateProfileResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();

        // Try direct fields first
        if (obj.contains(QLatin1String("nickname")) || obj.contains(QLatin1String("updated_at"))) {
            UpdateProfileResultDto result;
            result.nickname = obj.value(QLatin1String("nickname")).toString();
            result.avatar = obj.value(QLatin1String("avatar")).toString();
            result.updatedAt = obj.value(QLatin1String("updated_at")).toString();
            return result;
        }

        // Try nested "user" object
        const QJsonValue userVal = obj.value(QLatin1String("user"));
        if (userVal.isObject()) {
            const QJsonObject userObj = userVal.toObject();
            UpdateProfileResultDto result;
            result.nickname = userObj.value(QLatin1String("nickname")).toString();
            result.avatar = userObj.value(QLatin1String("avatar")).toString();
            result.updatedAt = userObj.value(QLatin1String("updated_at")).toString();
            return result;
        }

        // Success without specific data
        return UpdateProfileResultDto{};
    }

    /**
     * @brief Parse change password result from envelope data.
     *
     * @details
     * Expected shape: data = {} (empty object on success)
     */
    inline auto ParseChangePasswordResult(const QJsonValue& dataVal) -> std::optional<ChangePasswordResultDto> {
        // Any non-null data indicates success
        ChangePasswordResultDto result;
        result.success = true;
        return result;
    }

} // namespace disk::qml::models
