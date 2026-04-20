/**
 * @file ErrorAdapter.hpp
 * @brief Converts backend JSON errors to desktop ApiError model
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QJsonObject>
#include <QNetworkReply>
#include <QString>
#include <optional>

namespace disk::desktop {

    struct ApiError {
        int code{ 0 };
        QString family;
        QString category;
        QString message;
        bool retryable{ false };
        QString action;
        std::optional<QString> field;
        std::optional<QString> value;
    };

    class ErrorAdapter {
    public:
        static auto FromJson(const QJsonObject& json) -> ApiError;
        static auto FromNetworkError(QNetworkReply::NetworkError error) -> ApiError;

    private:
        static auto Classify(int code) -> QString;
        static auto MapToAction(int code) -> QString;
        static auto IsRetryable(int code) -> bool;
        static auto MapToCategory(int code) -> QString;
    };

} // namespace disk::desktop
