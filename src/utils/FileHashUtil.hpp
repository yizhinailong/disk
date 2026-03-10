/**
 * @file FileHashUtil.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件内容哈希工具类
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include <sodium/crypto_hash_sha256.h>

#include "utils/ErrorCode.hpp"

namespace disk::utils {

    /**
     * @brief 文件内容哈希工具类
     *
     * 提供文件内容的哈希计算功能：
     * - MD5 哈希：纯 C++ 实现，用于快速校验
     * - SHA256 哈希：使用 libsodium 实现，用于安全校验
     */
    class FileHashUtil {
    public:
        // ==================== MD5 哈希 ====================

        /**
         * @brief 计算数据的 MD5 哈希
         *
         * @param data 输入数据
         * @return std::string 32 字符小写十六进制字符串
         */
        [[nodiscard]]
        static auto HashMd5(const std::string& data) -> std::string {
            Md5Context context;
            Md5Init(context);
            Md5Update(context, reinterpret_cast<const uint8_t*>(data.data()), data.length());

            uint8_t digest[16];
            Md5Final(context, digest);

            return BytesToHex(digest, 16);
        }

        // ==================== SHA256 哈希 ====================

        /**
         * @brief 计算数据的 SHA256 哈希
         *
         * @param data 输入数据
         * @return std::string 64 字符小写十六进制字符串
         */
        [[nodiscard]]
        static auto HashSha256(const std::string& data) -> std::string {
            std::array<uint8_t, crypto_hash_sha256_BYTES> hash{};

            crypto_hash_sha256(
                hash.data(),
                reinterpret_cast<const unsigned char*>(data.data()),
                data.length()
            );

            return BytesToHex(hash.data(), hash.size());
        }

        // ==================== 文件哈希 ====================

        /**
         * @brief 计算文件的 MD5 哈希
         *
         * @param path 文件路径
         * @return Result<std::string> 成功返回 32 字符哈希，失败返回错误
         */
        [[nodiscard]]
        static auto HashFileMd5(const std::filesystem::path& path) -> Result<std::string> {
            if (!std::filesystem::exists(path)) {
                return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
            }

            std::ifstream file(path, std::ios::binary);
            if (!file) {
                return std::unexpected(ErrorInfo(ErrorCode::FileReadError));
            }

            Md5Context context;
            Md5Init(context);

            std::array<char, 8192> buffer{};
            while (file.read(buffer.data(), buffer.size())) {
                Md5Update(context, reinterpret_cast<uint8_t*>(buffer.data()), file.gcount());
            }
            if (file.gcount() > 0) {
                Md5Update(context, reinterpret_cast<uint8_t*>(buffer.data()), file.gcount());
            }

            uint8_t digest[16];
            Md5Final(context, digest);

            return BytesToHex(digest, 16);
        }

        /**
         * @brief 计算文件的 SHA256 哈希
         *
         * @param path 文件路径
         * @return Result<std::string> 成功返回 64 字符哈希，失败返回错误
         */
        [[nodiscard]]
        static auto HashFileSha256(const std::filesystem::path& path) -> Result<std::string> {
            if (!std::filesystem::exists(path)) {
                return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
            }

            std::ifstream file(path, std::ios::binary);
            if (!file) {
                return std::unexpected(ErrorInfo(ErrorCode::FileReadError));
            }

            crypto_hash_sha256_state state;
            crypto_hash_sha256_init(&state);

            std::array<char, 8192> buffer{};
            while (file.read(buffer.data(), buffer.size())) {
                crypto_hash_sha256_update(
                    &state,
                    reinterpret_cast<unsigned char*>(buffer.data()),
                    file.gcount()
                );
            }
            if (file.gcount() > 0) {
                crypto_hash_sha256_update(
                    &state,
                    reinterpret_cast<unsigned char*>(buffer.data()),
                    file.gcount()
                );
            }

            std::array<uint8_t, crypto_hash_sha256_BYTES> hash{};
            crypto_hash_sha256_final(&state, hash.data());

            return BytesToHex(hash.data(), hash.size());
        }

        // ==================== 哈希验证 ====================

        /**
         * @brief 验证数据的 MD5 哈希
         *
         * @param data 输入数据
         * @param expected_md5 预期的 MD5 哈希（小写）
         * @return bool 哈希是否匹配
         */
        [[nodiscard]]
        static auto VerifyHash(const std::string& data, const std::string& expected_md5) -> bool {
            auto actual = HashMd5(data);
            return actual == expected_md5;
        }

    private:
        // ==================== MD5 内部实现 ====================

        struct Md5Context {
            uint32_t state[4];
            uint32_t count[2];
            uint8_t buffer[64];
        };

        static constexpr uint32_t K[64] = {
            0xd76aa478,
            0xe8c7b756,
            0x242070db,
            0xc1bdceee,
            0xf57c0faf,
            0x4787c62a,
            0xa8304613,
            0xfd469501,
            0x698098d8,
            0x8b44f7af,
            0xffff5bb1,
            0x895cd7be,
            0x6b901122,
            0xfd987193,
            0xa679438e,
            0x49b40821,
            0xf61e2562,
            0xc040b340,
            0x265e5a51,
            0xe9b6c7aa,
            0xd62f105d,
            0x02441453,
            0xd8a1e681,
            0xe7d3fbc8,
            0x21e1cde6,
            0xc33707d6,
            0xf4d50d87,
            0x455a14ed,
            0xa9e3e905,
            0xfcefa3f8,
            0x676f02d9,
            0x8d2a4c8a,
            0xfffa3942,
            0x8771f681,
            0x6d9d6122,
            0xfde5380c,
            0xa4beea44,
            0x4bdecfa9,
            0xf6bb4b60,
            0xbebfbc70,
            0x289b7ec6,
            0xeaa127fa,
            0xd4ef3085,
            0x04881d05,
            0xd9d4d039,
            0xe6db99e5,
            0x1fa27cf8,
            0xc4ac5665,
            0xf4292244,
            0x432aff97,
            0xab9423a7,
            0xfc93a039,
            0x655b59c3,
            0x8f0ccc92,
            0xffeff47d,
            0x85845dd1,
            0x6fa87e4f,
            0xfe2ce6e0,
            0xa3014314,
            0x4e0811a1,
            0xf7537e82,
            0xbd3af235,
            0x2ad7d2bb,
            0xeb86d391
        };

        static constexpr uint32_t S[64] = {
            7,
            12,
            17,
            22,
            7,
            12,
            17,
            22,
            7,
            12,
            17,
            22,
            7,
            12,
            17,
            22,
            5,
            9,
            14,
            20,
            5,
            9,
            14,
            20,
            5,
            9,
            14,
            20,
            5,
            9,
            14,
            20,
            4,
            11,
            16,
            23,
            4,
            11,
            16,
            23,
            4,
            11,
            16,
            23,
            4,
            11,
            16,
            23,
            6,
            10,
            15,
            21,
            6,
            10,
            15,
            21,
            6,
            10,
            15,
            21,
            6,
            10,
            15,
            21
        };

        static void Md5Init(Md5Context& context) {
            context.state[0] = 0x67452301;
            context.state[1] = 0xefcdab89;
            context.state[2] = 0x98badcfe;
            context.state[3] = 0x10325476;
            context.count[0] = 0;
            context.count[1] = 0;
            std::memset(context.buffer, 0, 64);
        }

        static void Md5Transform(uint32_t state[4], const uint8_t block[64]) {
            uint32_t a = state[0];
            uint32_t b = state[1];
            uint32_t c = state[2];
            uint32_t d = state[3];
            uint32_t x[16];

            for (size_t i = 0; i < 16; ++i) {
                x[i] = static_cast<uint32_t>(block[i * 4]) |
                       (static_cast<uint32_t>(block[i * 4 + 1]) << 8) |
                       (static_cast<uint32_t>(block[i * 4 + 2]) << 16) |
                       (static_cast<uint32_t>(block[i * 4 + 3]) << 24);
            }

            for (size_t i = 0; i < 64; ++i) {
                uint32_t f;
                uint32_t g;

                if (i < 16) {
                    f = (b & c) | ((~b) & d);
                    g = i;
                } else if (i < 32) {
                    f = (d & b) | ((~d) & c);
                    g = (5 * i + 1) % 16;
                } else if (i < 48) {
                    f = b ^ c ^ d;
                    g = (3 * i + 5) % 16;
                } else {
                    f = c ^ (b | (~d));
                    g = (7 * i) % 16;
                }

                uint32_t temp = d;
                d = c;
                c = b;
                b = b + (((a + f + K[i] + x[g]) << S[i]) | ((a + f + K[i] + x[g]) >> (32 - S[i])));
                a = temp;
            }

            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;

            std::memset(x, 0, sizeof(x));
        }

        static void Md5Update(Md5Context& context, const uint8_t* data, size_t length) {
            size_t index = (context.count[0] >> 3) & 0x3F;
            size_t part_len = 64 - index;
            size_t i = 0;

            context.count[0] += static_cast<uint32_t>(length << 3);
            if (context.count[0] < (length << 3)) {
                context.count[1]++;
            }
            context.count[1] += static_cast<uint32_t>(length >> 29);

            if (length >= part_len) {
                std::memcpy(&context.buffer[index], data, part_len);
                Md5Transform(context.state, context.buffer);

                for (i = part_len; i + 63 < length; i += 64) {
                    Md5Transform(context.state, data + i);
                }

                index = 0;
            }

            std::memcpy(&context.buffer[index], data + i, length - i);
        }

        static void Md5Final(Md5Context& context, uint8_t digest[16]) {
            static constexpr uint8_t PADDING[64] = {
                0x80,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0
            };

            uint8_t bits[8];
            for (size_t i = 0; i < 4; ++i) {
                bits[i] = static_cast<uint8_t>((context.count[0] >> (i * 8)) & 0xFF);
                bits[i + 4] = static_cast<uint8_t>((context.count[1] >> (i * 8)) & 0xFF);
            }

            size_t index = (context.count[0] >> 3) & 0x3F;
            size_t pad_len = (index < 56) ? (56 - index) : (120 - index);

            Md5Update(context, PADDING, pad_len);
            Md5Update(context, bits, 8);

            for (size_t i = 0; i < 4; ++i) {
                digest[i] = static_cast<uint8_t>((context.state[0] >> (i * 8)) & 0xFF);
                digest[i + 4] = static_cast<uint8_t>((context.state[1] >> (i * 8)) & 0xFF);
                digest[i + 8] = static_cast<uint8_t>((context.state[2] >> (i * 8)) & 0xFF);
                digest[i + 12] = static_cast<uint8_t>((context.state[3] >> (i * 8)) & 0xFF);
            }

            std::memset(context.buffer, 0, 64);
        }

        static auto BytesToHex(const uint8_t* bytes, size_t length) -> std::string {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (size_t i = 0; i < length; ++i) {
                oss << std::setw(2) << static_cast<unsigned>(bytes[i]);
            }
            return oss.str();
        }
    };

} // namespace disk::utils
