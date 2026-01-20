/**
 * @file HashUtil.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 统一的哈希工具类（密码和 Token 哈希）
 * @version 0.1
 * @date 2026-01-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <array>
#include <iomanip>
#include <sstream>
#include <string>

#include <sodium/crypto_hash_sha256.h>
#include <sodium/crypto_pwhash.h>

#include "utils/ErrorCode.hpp"

namespace disk::utils {

    /**
     * @brief 统一的哈希工具类
     *
     * 提供密码和 Token 的哈希功能：
     * - 密码哈希：使用 Argon2id 算法，带随机 salt
     * - Token 哈希：使用 SHA256 算法，确定性输出
     */
    class HashUtil {
    public:
        // ==================== 密码哈希（Argon2id）====================

        /**
         * @brief 加密密码（使用 libsodium Argon2id 算法）
         *
         * 特性：
         * - 使用随机 salt，相同密码每次哈希结果不同
         * - 内存硬算法，抗 GPU/ASIC 攻击
         * - 适合交互式应用（OPSLIMIT_INTERACTIVE + MEMLIMIT_INTERACTIVE）
         *
         * @param password 明文密码
         * @return Result<std::string> 成功返回哈希字符串，失败返回错误信息
         */
        [[nodiscard]]
        static auto HashPassword(const std::string& password) -> Result<std::string> {
            if (password.empty()) {
                LOG_ERROR << "密码哈希失败: 密码为空";
                return std::unexpected(ErrorInfo(ErrorCode::InternalError, "密码不能为空"));
            }

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
         *
         * @param password 明文密码
         * @param hash 存储的哈希值
         * @return bool 密码是否匹配
         */
        [[nodiscard]]
        static auto VerifyPassword(const std::string& password, const std::string& hash) -> bool {
            LOG_DEBUG << "开始密码验证";
            auto result = crypto_pwhash_str_verify(hash.c_str(), password.c_str(), password.length()) == 0;
            LOG_DEBUG << "密码验证完成: " << (result ? "成功" : "失败");
            return result;
        }

        // ==================== Token 哈希（SHA256）====================

        /**
         * @brief Token 哈希结果（32 字节 SHA256）
         */
        using TokenHash = std::array<uint8_t, 32>;

        /**
         * @brief 计算 Token 的 SHA256 哈希
         *
         * 特性：
         * - 确定性：相同输入总是产生相同输出
         * - 用于存储 token 的指纹而非明文
         * - 支持去重和快速比对
         *
         * @param token Token 字符串
         * @return Result<TokenHash> 32 字节哈希值，失败返回错误信息
         */
        [[nodiscard]]
        static auto HashToken(const std::string& token) -> Result<TokenHash> {
            if (token.empty()) {
                LOG_ERROR << "Token 哈希失败: Token 为空";
                return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Token 不能为空"));
            }

            TokenHash hash{};

            // NOLINTNEXTLINE: Required by libsodium crypto_hash_sha256 API
            const auto* data = reinterpret_cast<const unsigned char*>(token.c_str());

            if (crypto_hash_sha256(
                    hash.data(),
                    data,
                    token.length()
                ) != 0) {
                LOG_ERROR << "Token 哈希计算失败";
                return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Token 哈希计算失败"));
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
        static auto TokenHashToHex(const TokenHash& hash) -> std::string {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (auto byte : hash) {
                oss << std::setw(2) << static_cast<unsigned>(byte);
            }
            return oss.str();
        }
    };

} // namespace disk::utils
