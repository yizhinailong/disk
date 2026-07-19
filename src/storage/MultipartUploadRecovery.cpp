#include "storage/MultipartUploadRecovery.hpp"

#include "utils/FileHashUtil.hpp"

namespace disk::storage {

    auto BuildMultipartUploadRecoveryId(const MultipartUploadDescriptor& descriptor) -> std::string {
        return disk::utils::FileHashUtil::HashSha256(
            descriptor.key + "\n" + descriptor.upload_id
        );
    }

    auto BuildMultipartUploadRecoveryDedupeKey(const MultipartUploadDescriptor& descriptor)
        -> std::string {
        return "multipart-abort:" + BuildMultipartUploadRecoveryId(descriptor);
    }

} // namespace disk::storage
