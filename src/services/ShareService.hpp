/**
 * @file ShareService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享服务
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 提供分享模块相关功能：
 * - 创建分享（生成分享码、密码加密、设置有效期）
 * - 获取分享列表（支持状态过滤、分页）
 * - 获取分享详情
 * - 更新分享设置
 * - 批量取消分享（混合结果响应）
 * - 验证分享访问（密码验证、生成分享令牌）
 * - 浏览分享内容（文件/文件夹列表、面包屑导航）
 * - 下载元数据（文件信息、路径）
 *
 * 业务规则：
 * - 分享码（share_code）作为外部标识符，不暴露内部 id
 * - 分享状态：0=已取消，1=有效，2=已过期
 * - 权限类型：view=仅查看，download=可下载
 * - 密码使用 Argon2id 加密存储
 * - 访问时验证密码并生成分享令牌
 * - 浏览和下载需要有效的分享令牌
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <drogon/nosql/RedisClient.h>
#include <drogon/orm/DbClient.h>

#include "dtos/ShareDto.hpp"
#include "dtos/FileDto.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/Shares.hpp"
#include "services/RedisService.hpp"
#include "services/TokenService.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::share {

    /**
     * @brief 分享服务类
     *
     * @details
     * 提供分享模块的业务逻辑处理：
     * - Create: 创建分享，验证文件所有权，生成分享码
     * - List: 查询用户的分享列表（分页、状态过滤）
     * - Detail: 获取分享详情和关联文件列表
     * - Update: 更新分享设置（有效期、密码、权限）
     * - Cancel: 批量取消分享，返回混合结果
     * - Access: 验证分享访问权限，生成分享令牌
     * - Browse: 浏览分享内容（需分享令牌）
     * - DownloadMeta: 获取下载文件元数据（需分享令牌）
     *
     * 安全规则：
     * - 密码验证失败限制（5次/15分钟/IP/分享）
     * - 分享令牌有效期（1小时）
     * - 下载次数统计
     */
    class ShareService {
    public:
        /**
         * @brief 构造函数
         * @param db_client 数据库客户端
         * @param redis_client Redis客户端
         * @param jwt_secret JWT密钥
         */
        explicit ShareService(
            drogon::orm::DbClientPtr db_client,
            drogon::nosql::RedisClientPtr redis_client,
            std::string jwt_secret
        );
        ~ShareService() = default;
        ShareService(const ShareService&) = delete;
        auto operator=(const ShareService&) -> ShareService& = delete;
        ShareService(ShareService&&) = default;
        auto operator=(ShareService&&) -> ShareService& = default;

        /**
         * @brief 创建分享
         *
         * 业务规则：
         * - 验证所有文件 ID 属于当前用户
         * - 生成唯一的分享码（share_code）
         * - 如果设置密码，使用 Argon2id 加密存储
         * - 根据有效期天数计算 expires_at（0表示永久）
         * - 创建 share_files 关联记录
         *
         * @param request 创建分享请求
         * @param user_id 用户 ID
         * @return drogon::Task<Result<CreateShareResponse>> 成功返回分享信息，失败返回错误
         */
        [[nodiscard]]
        auto Create(CreateShareRequest request, uint64_t user_id)
            -> drogon::Task<Result<CreateShareResponse>>;

        /**
         * @brief 获取分享列表
         *
         * 业务规则：
         * - 支持状态过滤：all/active/expired/cancelled
         * - 按 created_at DESC 排序
         * - 返回分页结果
         *
         * @param request 分享列表请求
         * @param user_id 用户 ID
         * @return drogon::Task<Result<ShareListResponse>> 成功返回分享列表，失败返回错误
         */
        [[nodiscard]]
        auto List(const ShareListRequest& request, uint64_t user_id)
            -> drogon::Task<Result<ShareListResponse>>;

        /**
         * @brief 获取分享详情
         *
         * 业务规则：
         * - 验证分享归属
         * - 返回分享信息和关联文件列表
         * - 不验证分享状态（允许查看已取消/已过期的分享详情）
         *
         * @param request 分享详情请求
         * @param user_id 用户 ID
         * @return drogon::Task<Result<ShareDetailResponse>> 成功返回分享详情，失败返回错误
         */
        [[nodiscard]]
        auto Detail(const ShareDetailRequest& request, uint64_t user_id)
            -> drogon::Task<Result<ShareDetailResponse>>;

        /**
         * @brief 更新分享设置
         *
         * 业务规则：
         * - 验证分享归属和状态（必须是有效状态）
         * - 更新有效期、密码、权限
         * - 空密码表示移除密码保护
         * - 更新后重置 expires_at
         *
         * @param request 更新分享请求
         * @param user_id 用户 ID
         * @return drogon::Task<Result<UpdateShareResponse>> 成功返回更新后的分享信息，失败返回错误
         */
        [[nodiscard]]
        auto Update(const UpdateShareRequest& request, uint64_t user_id)
            -> drogon::Task<Result<UpdateShareResponse>>;

        /**
         * @brief 批量取消分享
         *
         * 业务规则：
         * - 验证每个分享的归属
         * - 将状态设置为已取消
         * - 返回混合结果：成功数、失败数、每项结果
         * - 不存在的分享标记为失败，不影响其他分享
         *
         * @param request 取消分享请求
         * @param user_id 用户 ID
         * @return drogon::Task<Result<CancelShareResponse>> 成功返回批量取消结果，失败返回错误
         */
        [[nodiscard]]
        auto Cancel(const CancelShareRequest& request, uint64_t user_id)
            -> drogon::Task<Result<CancelShareResponse>>;

        /**
         * @brief 验证分享访问
         *
         * 业务规则：
         * - 验证分享存在且状态为有效
         * - 验证是否过期
         * - 如果设置了密码，验证密码正确性
         * - 密码验证失败记录尝试次数
         * - 验证成功生成分享令牌
         * - 增加访问次数
         *
         * @param request 访问分享请求
         * @param ip_address 客户端IP地址
         * @return drogon::Task<Result<AccessShareResponse>> 成功返回分享令牌，失败返回错误
         */
        [[nodiscard]]
        auto Access(const AccessShareRequest& request, const std::string& ip_address)
            -> drogon::Task<Result<AccessShareResponse>>;

        /**
         * @brief 浏览分享内容
         *
         * 业务规则：
         * - 验证分享令牌有效性
         * - 返回分享的文件/文件夹列表
         * - 支持文件夹导航（breadcrumb）
         * - 仅返回分享中包含的文件
         *
         * @param request 浏览分享请求
         * @param share_id 分享内部ID
         * @return drogon::Task<Result<BrowseShareResponse>> 成功返回浏览结果，失败返回错误
         */
        [[nodiscard]]
        auto Browse(const BrowseShareRequest& request, uint64_t share_id)
            -> drogon::Task<Result<BrowseShareResponse>>;

        /**
         * @brief 获取下载文件元数据
         *
         * 业务规则：
         * - 验证分享令牌有效性
         * - 验证权限（必须为 download）
         * - 验证文件属于分享内容
         * - 返回文件存储信息
         * - 增加下载次数
         *
         * @param request 下载分享请求
         * @param share_id 分享内部ID
         * @return drogon::Task<Result<ShareFile>> 成功返回文件元数据，失败返回错误
         */
        [[nodiscard]]
        auto DownloadMeta(const DownloadShareRequest& request, uint64_t share_id)
            -> drogon::Task<Result<ShareFile>>;

        /**
         * @brief 获取下载文件完整信息（包含存储路径）
         *
         * 业务规则：
         * - 验证文件属于分享内容
         * - 验证分享权限（必须为 download）
         * - 返回文件存储路径、大小、MIME 类型
         * - 不增加下载次数（由控制器在成功下载后调用）
         *
         * @param request 下载分享请求
         * @param share_id 分享内部ID
         * @return drogon::Task<Result<DownloadInfo>> 成功返回下载信息，失败返回错误
         */
        [[nodiscard]]
        auto GetDownloadInfo(const DownloadShareRequest& request, uint64_t share_id)
            -> drogon::Task<Result<DownloadInfo>>;

        [[nodiscard]]
        auto SaveToDrive(const SaveShareItemsRequest& request, uint64_t share_id, uint64_t target_user_id)
            -> drogon::Task<Result<SaveShareItemsResponse>>;

        /**
         * @brief 根据分享码查找分享
         *
         * @param share_code 分享码
         * @return drogon::Task<Result<drogon_model::disk::Shares>> 成功返回分享模型，失败返回错误
         */
        [[nodiscard]]
        auto FindShareByCode(const std::string& share_code) const
            -> drogon::Task<Result<drogon_model::disk::Shares>>;

        /**
         * @brief 增加下载次数
         * @param share_id 分享内部ID
         * @return drogon::Task<void>
         */
        auto IncrementDownloadCount(uint64_t share_id) -> drogon::Task<void>;

    private:
        /**
         * @brief 生成唯一的分享码
         * @return std::string 分享码
         */
        [[nodiscard]]
        static auto GenerateShareCode() -> std::string;

        /**
         * @brief 验证文件所有权
         * @param file_ids 文件ID列表
         * @param user_id 用户ID
         * @return drogon::Task<Result<std::vector<drogon_model::disk::Files>>> 成功返回文件列表
         */
        [[nodiscard]]
        auto ValidateFileOwnership(const std::vector<uint64_t>& file_ids, uint64_t user_id) const
            -> drogon::Task<Result<std::vector<drogon_model::disk::Files>>>;

        [[nodiscard]]
        auto ValidateFolderOwnership(const std::vector<uint64_t>& folder_ids, uint64_t user_id) const
            -> drogon::Task<Result<std::vector<drogon_model::disk::Folders>>>;

        /**
         * @brief 验证分享所有权
         * @param share_code 分享码
         * @param user_id 用户ID
         * @return drogon::Task<Result<drogon_model::disk::Shares>> 成功返回分享模型
         */
        [[nodiscard]]
        auto ValidateShareOwnership(const std::string& share_code, uint64_t user_id) const
            -> drogon::Task<Result<drogon_model::disk::Shares>>;

        /**
         * @brief 获取分享关联的文件列表
         * @param share_id 分享内部ID
         * @return drogon::Task<std::vector<ShareFile>> 文件列表
         */
        [[nodiscard]]
        auto GetShareFiles(uint64_t share_id) const -> drogon::Task<std::vector<ShareFile>>;

        [[nodiscard]]
        auto GetShareFilesBatch(const std::vector<uint64_t>& share_ids) const
            -> drogon::Task<std::unordered_map<uint64_t, std::vector<ShareFile>>>;

        /**
         * @brief 检查分享是否过期
         * @param share 分享模型
         * @return bool 是否过期
         */
        [[nodiscard]]
        static auto IsShareExpired(const drogon_model::disk::Shares& share) -> bool;

        /**
         * @brief 检查分享状态是否为有效
         * @param share 分享模型
         * @return bool 是否有效
         */
        [[nodiscard]]
        static auto IsShareActive(const drogon_model::disk::Shares& share) -> bool;

        /**
         * @brief 验证分享密码
         * @param share 分享模型
         * @param password 用户输入的密码
         * @return bool 密码是否正确
         */
        [[nodiscard]]
        static auto VerifyPassword(const drogon_model::disk::Shares& share, const std::string& password)
            -> bool;

        /**
         * @brief 增加访问次数
         * @param share_id 分享内部ID
         * @return drogon::Task<void>
         */
        auto IncrementViewCount(uint64_t share_id) -> drogon::Task<void>;

        /**
         * @brief 更新分享的 updated_at 时间戳
         * @param share_id 分享内部ID
         * @return drogon::Task<void>
         */
        auto UpdateTimestamp(uint64_t share_id) -> drogon::Task<void>;

        /**
         * @brief 获取状态过滤条件
         * @param status 状态字符串
         * @return std::optional<int8_t> 状态值（nullopt表示不过滤）
         */
        [[nodiscard]]
        static auto GetStatusFilter(const std::string& status) -> std::optional<int8_t>;

        /**
         * @brief 格式化日期时间字符串
         * @param date 日期对象
         * @return std::string 格式化的日期时间字符串
         */
        [[nodiscard]]
        static auto FormatDateTime(const trantor::Date& date) -> std::string;

        /**
         * @brief 构建分享链接
         * @param share_code 分享码
         * @return std::string 分享链接
         */
        [[nodiscard]]
        static auto BuildShareLink(const std::string& share_code) -> std::string;

        /**
         * @brief 检查密码尝试次数限制
         * @param share_code 分享码
         * @param ip_address IP地址
         * @return drogon::Task<Result<void>> 成功表示未超限
         */
        [[nodiscard]]
        auto CheckPasswordRateLimit(const std::string& share_code, const std::string& ip_address) const
            -> drogon::Task<Result<void>>;

    private:
        drogon::orm::DbClientPtr m_db_client;                          ///< 数据库客户端
        drogon::nosql::RedisClientPtr m_redis_client;                  ///< Redis客户端
        std::shared_ptr<disk::services::RedisService> m_redis_service; ///< Redis服务
        std::string m_jwt_secret;                                      ///< JWT密钥
    };

} // namespace disk::share
