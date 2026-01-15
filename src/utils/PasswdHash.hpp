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
        LOG_DEBUG << "开始密码哈希计算";

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
            LOG_ERROR << "密码哈希失败: 内存不足";
            return std::unexpected(ErrorInfo(ErrorCode::InternalError, "内存不足，密码哈希失败"));
        }

        LOG_DEBUG << "密码哈希计算完成";
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
        LOG_DEBUG << "开始密码验证";
        auto result = crypto_pwhash_str_verify(hash.c_str(), password.c_str(), password.length()) == 0;
        LOG_DEBUG << "密码验证完成: " << (result ? "成功" : "失败");
        return result;
    }

} // namespace disk::utils::passwd

namespace PasswdHash = disk::utils::passwd;
