#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QString>
#include <optional>

namespace disk::qml::api {

    /// Transport-layer and HTTP-status error information.
    struct ApiError {
        bool hasNetworkError{ false };
        QNetworkReply::NetworkError networkError{ QNetworkReply::NoError };
        QString networkErrorString;
        int httpStatus{ 0 };
        bool httpStatusSuccess{ false }; ///< true when httpStatus is 200..299
    };

    /// Structured response returned by all API calls.
    struct ApiReply {
        ApiError error;
        QByteArray body;
        std::optional<QJsonDocument> json;
        QJsonParseError jsonParseError;
    };

} // namespace disk::qml::api
