/**
 * @file ApiEnvelope.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Shared API response envelope for the QML client
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <functional>
#include <optional>

namespace disk::qml::models {

    // ==================== API Envelope ====================

    /**
     * @brief Uniform JSON envelope: { "code": 0, "message": "success", "data": ... }
     *
     * @details
     * All API responses are wrapped in this envelope.
     * The `data` field holds QJsonValue::Null when absent or explicitly null.
     *
     * Mirrors the backend structure defined in src/utils/Response.hpp:
     * - code 0 = success
     * - code 10xxx = generic errors
     * - code 40xxx = auth errors
     * - code 50xxx = file errors
     * - code 60xxx = share errors
     */
    struct ApiEnvelope {
        int code{};
        QString message;
        QJsonValue data; ///< Payload; QJsonValue::Null when absent / null

        /// @brief Check whether the envelope represents a successful response (code == 0).
        [[nodiscard]] auto IsSuccess() const noexcept -> bool { return code == 0; }

        /// @brief Check whether the envelope represents an error response (code != 0).
        [[nodiscard]] auto IsError() const noexcept -> bool { return code != 0; }
    };

    // ==================== Envelope Parsing ====================

    /**
     * @brief Parse a raw JSON document into an ApiEnvelope.
     *
     * @details
     * Expected shape: { "code": <int>, "message": "<string>", "data": <any> }
     *
     * @param doc  Parsed QJsonDocument from the HTTP response body.
     * @return     Populated ApiEnvelope, or std::nullopt if the document is not
     *             an object or if "code" / "message" fields are absent.
     */
    inline auto ParseEnvelope(const QJsonDocument& doc) -> std::optional<ApiEnvelope> {
        if (!doc.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = doc.object();
        if (!obj.contains(QLatin1String("code")) ||
            !obj.contains(QLatin1String("message"))) {
            return std::nullopt;
        }

        ApiEnvelope env;
        env.code = obj.value(QLatin1String("code")).toInt();
        env.message = obj.value(QLatin1String("message")).toString();
        env.data = obj.value(QLatin1String("data")); // QJsonValue::Undefined → Null
        return env;
    }

    /**
     * @brief Parse a raw byte array into an ApiEnvelope.
     *
     * @details
     * Convenience overload that handles JSON deserialization from raw bytes.
     *
     * @param body  Raw response body bytes from HTTP reply.
     * @return      Populated ApiEnvelope, or std::nullopt if the body is empty,
     *              cannot be parsed as JSON, or lacks envelope fields.
     */
    inline auto ParseEnvelope(const QByteArray& body) -> std::optional<ApiEnvelope> {
        if (body.isEmpty()) {
            return std::nullopt;
        }

        QJsonParseError parseError;
        const QJsonDocument json = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            return std::nullopt;
        }

        return ParseEnvelope(json);
    }

    // ==================== Typed Data Extraction ====================

    /**
     * @brief Extract the `data` field from an envelope as a QJsonObject.
     *
     * @param env  The API envelope to extract from.
     * @return     The data object, or std::nullopt if `data` is not a JSON object.
     */
    inline auto EnvelopeDataObject(const ApiEnvelope& env) -> std::optional<QJsonObject> {
        if (!env.data.isObject()) {
            return std::nullopt;
        }
        return env.data.toObject();
    }

    // ==================== Generic Callback Type ====================

    /**
     * @brief Generic callback type for API endpoints.
     *
     * @details
     * - On success: `envelope` is populated, `networkError` is empty.
     * - On network error: `envelope` is default-constructed, `networkError` describes the issue.
     *
     * All API modules (AuthApi, FileApi, FolderApi, etc.) should use this callback type.
     */
    using ApiCallback = std::function<void(ApiEnvelope envelope, QString networkError)>;

    // ==================== Reply → Envelope Helper ====================

    /**
     * @brief Parse a raw HTTP reply into an ApiEnvelope and invoke the callback.
     *
     * @details
     * Shared helper that centralises the byte-array → envelope parsing pipeline.
     * Handles network errors, empty bodies, JSON parse failures, and invalid envelopes.
     * Suitable for use inside any API module's reply lambda.
     *
     * @param hasNetworkError      True if a network-level error occurred.
     * @param networkErrorString   Human-readable description of the network error.
     * @param body                 Raw response body bytes.
     * @param cb                   Callback to invoke with the result.
     */
    inline auto ParseEnvelopeFromReply(
        bool hasNetworkError,
        const QString& networkErrorString,
        const QByteArray& body,
        ApiCallback& cb
    ) -> void {
        if (hasNetworkError) {
            cb(ApiEnvelope{}, networkErrorString);
            return;
        }

        auto envelope = ParseEnvelope(body);
        if (!envelope) {
            cb(ApiEnvelope{}, QStringLiteral("Failed to parse response JSON"));
            return;
        }

        cb(std::move(*envelope), QString{});
    }

} // namespace disk::qml::models
