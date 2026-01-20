/**
 * @file TokenHash.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Token 哈希工具函数
 * @version 0.1
 * @date 2026-01-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <array>
#include <iomanip>
#include <span>
#include <sstream>
#include <vector>

#include <sodium/crypto_hash_sha256.h>
#include <sodium/randombytes.h>
#include <sodium/utils.h>

namespace disk::utils::token {

    /**
     * @brief Token 哈希结果（32 字节 SHA256）
     */
    using TokenHash = std::array<uint8_t, 32>;

    /**
     * @brief 计算 Token 的 SHA256 哈希
     *
     * @param token Token 字符串
     * @return TokenHash 32 字节哈希值
     */
    [[nodiscard]]
    inline auto Hash(const std::string& token) -> TokenHash {
        TokenHash hash{};

        if (crypto_hash_sha256(
                hash.data(),
                reinterpret_cast<const unsigned char*>(token.c_str()),
                token.length()
            ) != 0) {
            throw std::runtime_error("Token hash computation failed");
        }

        return hash;
    }

    /**
     * @brief 将哈希值转换为十六进制字符串
     *
     * @param hash 32 字节哈希值
     * @return std::string 十六进制字符串（64 字符）
     */
    [[nodiscard]]
    inline auto ToHex(const TokenHash& hash) -> std::string {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (auto byte : hash) {
            oss << std::setw(2) << static_cast<unsigned>(byte);
        }
        return oss.str();
    }

} // namespace disk::utils::token

namespace TokenHashUtils = disk::utils::token;
