/**
 * @file PasswdHash.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 密码哈希工具函数
 * @version 0.1
 * @date 2026-01-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "utils/ErrorCode.hpp"

namespace disk::utils::passwd {

    /**
     * @brief 加密密码（使用 libsodium Argon2id 算法）
     * @param password 明文密码
     * @return Result<std::string> 成功返回哈希字符串，失败返回错误信息
     */
    [[nodiscard]]
    inline auto Hash(const std::string& password) -> Result<std::string> {
        // 使用 libsodium 的 Argon2id 算法
        // crypto_pwhash_STRBYTES 是输出缓冲区的大小（128 字节）
        std::array<char, crypto_pwhash_STRBYTES> hashed_password{};

        if (crypto_pwhash_str(
                hashed_password.data(),
                password.c_str(),
                password.length(),
                crypto_pwhash_OPSLIMIT_INTERACTIVE, // 适合交互式应用的计算强度
                crypto_pwhash_MEMLIMIT_INTERACTIVE  // 适合交互式应用的内存限制
                ) != 0) {
            return std::unexpected(ErrorInfo{ ErrorCode::InternalError, "内存不足，密码哈希失败" });
        }

        return std::string(hashed_password.data());
    }

    /**
     * @brief 验证密码
     * @param password 明文密码
     * @param hash 存储的哈希值
     * @return 密码是否匹配
     */
    [[nodiscard]]
    inline auto Verify(const std::string& password, const std::string& hash) -> bool {
        // 使用 libsodium 验证密码
        // crypto_pwhash_str_verify 会自动识别存储的哈希格式（Argon2id）
        return crypto_pwhash_str_verify(hash.c_str(), password.c_str(), password.length()) == 0;
    }

} // namespace disk::utils::passwd

namespace PasswdHash = disk::utils::passwd;
