/**
 * @file NameValidation.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件/目录名称校验工具
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace disk::utils {

    /**
     * @brief 判断字节序列是否为合法 UTF-8，并排除 Unicode 控制字符。
     *
     * @details
     * 允许 ASCII 与合法多字节 UTF-8 字符；拒绝过长编码、代理项、超出 Unicode
     * 范围的码点、孤立续字节，以及 C0/C1 控制字符。
     */
    [[nodiscard]]
    inline auto IsValidUtf8WithoutControlChars(std::string_view value) -> bool {
        std::size_t index{ 0 };
        while (index < value.size()) {
            const auto lead = static_cast<uint8_t>(value[index]);
            uint32_t code_point{ 0 };
            std::size_t sequence_length{ 0 };

            if (lead <= 0x7F) {
                code_point = lead;
                sequence_length = 1;
            } else if (lead >= 0xC2 && lead <= 0xDF) {
                code_point = lead & 0x1F;
                sequence_length = 2;
            } else if (lead >= 0xE0 && lead <= 0xEF) {
                code_point = lead & 0x0F;
                sequence_length = 3;
            } else if (lead >= 0xF0 && lead <= 0xF4) {
                code_point = lead & 0x07;
                sequence_length = 4;
            } else {
                return false;
            }

            if (index + sequence_length > value.size()) {
                return false;
            }

            for (std::size_t offset{ 1 }; offset < sequence_length; ++offset) {
                const auto continuation = static_cast<uint8_t>(value[index + offset]);
                if ((continuation & 0xC0) != 0x80) {
                    return false;
                }
                code_point = (code_point << 6) | (continuation & 0x3F);
            }

            if ((sequence_length == 2 && code_point < 0x80)
                || (sequence_length == 3 && code_point < 0x800)
                || (sequence_length == 4 && code_point < 0x10000)) {
                return false;
            }
            if ((code_point >= 0xD800 && code_point <= 0xDFFF) || code_point > 0x10FFFF) {
                return false;
            }
            if (code_point <= 0x1F || (code_point >= 0x7F && code_point <= 0x9F)) {
                return false;
            }

            index += sequence_length;
        }

        return true;
    }

    /**
     * @brief 校验文件系统不安全字符。
     *
     * @details
     * 保留原有跨平台约束：禁止 / \ : * ? " < > |，并禁止控制字符。
     */
    [[nodiscard]]
    inline auto HasForbiddenDriveItemChars(std::string_view value) -> bool {
        static constexpr std::string_view FORBIDDEN_CHARS{ "/\\:*?\"<>|" };
        return std::any_of(value.begin(), value.end(), [](const char c) {
            const auto byte = static_cast<uint8_t>(c);
            return byte <= 0x1F || FORBIDDEN_CHARS.find(c) != std::string_view::npos;
        });
    }

} ///< namespace disk::utils
