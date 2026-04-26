/**
 * @file TestJsonLoader.hpp
 * @brief Utility for loading JSON fixture files from tests/fixtures/json/
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace disk::desktop::testing {

    /**
     * @brief Loads JSON fixture files from the tests/fixtures/json/ directory
     *
     * Fixture paths are relative to the fixtures/json/ directory root.
     * Example: LoadJson("auth/login_success.json")
     */
    class TestJsonLoader {
    public:
        static auto LoadJson(const QString& relativePath) -> QJsonObject {
            // Search in multiple candidate locations for the fixture file
            // 1. Relative to build directory (fixtures copied as resources)
            // 2. Relative to source directory (original fixture location)
            QStringList candidates = {
                QStringLiteral(":/fixtures/json/") + relativePath,
                QStringLiteral("fixtures/json/") + relativePath,
                QStringLiteral("../../tests/fixtures/json/") + relativePath,
            };

            // Also try the source directory if QT_TEST_SOURCE_DIR is set
#ifdef QT_TEST_SOURCE_DIR
            candidates.prepend(QStringLiteral(QT_TEST_SOURCE_DIR) + "/fixtures/json/" + relativePath);
#endif

            for (const auto& path : candidates) {
                QFile file(path);
                if (file.open(QIODevice::ReadOnly)) {
                    auto doc = QJsonDocument::fromJson(file.readAll());
                    if (doc.isObject()) {
                        return doc.object();
                    }
                }
            }

            return {};
        }

        static auto LoadJsonArray(const QString& relativePath) -> QJsonArray {
            QStringList candidates = {
                QStringLiteral("fixtures/json/") + relativePath,
                QStringLiteral("../../tests/fixtures/json/") + relativePath,
            };

#ifdef QT_TEST_SOURCE_DIR
            candidates.prepend(QStringLiteral(QT_TEST_SOURCE_DIR) + "/fixtures/json/" + relativePath);
#endif

            for (const auto& path : candidates) {
                QFile file(path);
                if (file.open(QIODevice::ReadOnly)) {
                    auto doc = QJsonDocument::fromJson(file.readAll());
                    if (doc.isArray()) {
                        return doc.array();
                    }
                }
            }

            return {};
        }
    };

} // namespace disk::desktop::testing
