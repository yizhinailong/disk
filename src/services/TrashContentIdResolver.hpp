#pragma once

#include <optional>
#include <string>

#include <json/json.h>

#include "utils/ErrorCode.hpp"

namespace disk::services::trash_content_internal {

    enum class ContentIdSource {
        Column,
        ItemData,
    };

    struct ResolvedContentId {
        uint64_t value{ 0 };
        ContentIdSource source{ ContentIdSource::Column };
    };

    [[nodiscard]] inline auto ResolveRequiredContentId(
        const std::optional<uint64_t>& column_content_id,
        const std::string& item_data
    ) -> Result<ResolvedContentId> {
        if (column_content_id.has_value() && column_content_id.value() > 0) {
            return ResolvedContentId{
                .value = column_content_id.value(),
                .source = ContentIdSource::Column,
            };
        }

        Json::Value parsed_item_data;
        Json::Reader reader;
        if (!reader.parse(item_data, parsed_item_data) || !parsed_item_data.isObject()) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Trash item is missing valid content_id"
            ));
        }

        const auto& legacy_content_id = parsed_item_data["content_id"];
        if (!legacy_content_id.isUInt64()) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Trash item is missing valid content_id"
            ));
        }

        auto content_id = legacy_content_id.asUInt64();
        if (content_id == 0) {
            return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Trash item is missing valid content_id"
            ));
        }

        return ResolvedContentId{
            .value = content_id,
            .source = ContentIdSource::ItemData,
        };
    }

} ///< namespace disk::services::trash_content_internal
