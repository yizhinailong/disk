#include "services/ContentService.hpp"

#include <gtest/gtest.h>

namespace disk::content {
    namespace {

        TEST(ContentServiceCompileTest, CanConstructWithNullDbClient) {
            ContentService service(nullptr);
            SUCCEED();
        }

        TEST(ContentServiceContractTest, NewContentDefaultsToSingleReference) {
            NewContent content;
            content.hash_md5 = "0123456789abcdef0123456789abcdef";
            content.hash_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
            content.size = 42;
            content.storage_path = "build/uploaded/01/blob.bin";

            EXPECT_EQ(content.ref_count, 1);
            EXPECT_EQ(content.size, 42U);
            EXPECT_EQ(content.hash_md5.size(), 32U);
            EXPECT_EQ(content.hash_sha256.size(), 64U);
        }

        TEST(ContentServiceContractTest, ZeroRefContentCarriesVerifiedDeletionCandidate) {
            ZeroRefContent candidate{ .id = 7, .storage_path = "build/uploaded/aa/blob.bin" };

            EXPECT_EQ(candidate.id, 7U);
            EXPECT_EQ(candidate.storage_path, "build/uploaded/aa/blob.bin");
        }

    } ///< namespace
} ///< namespace disk::content
