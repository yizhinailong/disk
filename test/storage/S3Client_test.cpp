#include "storage/S3Client.hpp"

#include <gtest/gtest.h>

namespace disk::storage {

    TEST(S3ClientRetryPolicyTest, RetriesTimeoutThrottleServerAndConnectionFailures) {
        EXPECT_EQ(ClassifyS3Failure(408, {}, false), S3FailureClass::Retryable);
        EXPECT_EQ(ClassifyS3Failure(429, {}, false), S3FailureClass::Retryable);
        EXPECT_EQ(ClassifyS3Failure(500, {}, false), S3FailureClass::Retryable);
        EXPECT_EQ(ClassifyS3Failure(503, {}, false), S3FailureClass::Retryable);
        EXPECT_EQ(ClassifyS3Failure(599, {}, false), S3FailureClass::Retryable);
        EXPECT_EQ(ClassifyS3Failure(-1, "NetworkConnection", false), S3FailureClass::Retryable);
        EXPECT_EQ(ClassifyS3Failure(-1, {}, true), S3FailureClass::Retryable);
    }

    TEST(S3ClientRetryPolicyTest, DoesNotRetryAuthenticationParameterOrOrdinaryClientFailures) {
        EXPECT_EQ(ClassifyS3Failure(401, {}, true), S3FailureClass::Permanent);
        EXPECT_EQ(ClassifyS3Failure(403, "AccessDenied", true), S3FailureClass::Permanent);
        EXPECT_EQ(ClassifyS3Failure(400, "InvalidArgument", true), S3FailureClass::Permanent);
        EXPECT_EQ(ClassifyS3Failure(403, "SignatureDoesNotMatch", true), S3FailureClass::Permanent);
        EXPECT_EQ(ClassifyS3Failure(404, "NoSuchBucket", true), S3FailureClass::Permanent);
        EXPECT_EQ(ClassifyS3Failure(409, {}, true), S3FailureClass::Permanent);
    }

    TEST(S3ClientRetryPolicyTest, StopsAtConfiguredRetryBudget) {
        EXPECT_TRUE(ShouldRetryS3Failure(503, {}, false, 0, 3));
        EXPECT_TRUE(ShouldRetryS3Failure(503, {}, false, 1, 3));
        EXPECT_TRUE(ShouldRetryS3Failure(503, {}, false, 2, 3));
        EXPECT_FALSE(ShouldRetryS3Failure(503, {}, false, 3, 3));
        EXPECT_FALSE(ShouldRetryS3Failure(503, {}, false, 0, 0));
        EXPECT_FALSE(ShouldRetryS3Failure(403, "AccessDenied", true, 0, 3));
    }

} // namespace disk::storage
