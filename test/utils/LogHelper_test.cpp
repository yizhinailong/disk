#include "utils/LogHelper.hpp"

#include <memory>
#include <sstream>
#include <string>

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>

namespace disk::utils::test {

    class LogHelperTest : public ::testing::Test {
    protected:
        auto SetUp() -> void override {
            m_previous_logger = spdlog::default_logger();
            m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
            m_logger = std::make_shared<spdlog::logger>("log-helper-test", m_sink);
            m_logger->set_pattern("%v");
            spdlog::set_default_logger(m_logger);
        }

        auto TearDown() -> void override {
            spdlog::set_default_logger(m_previous_logger);
        }

        [[nodiscard]] auto Output() -> std::string {
            m_logger->flush();
            return m_output.str();
        }

        std::shared_ptr<spdlog::logger> m_previous_logger;
        std::ostringstream m_output;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
        std::shared_ptr<spdlog::logger> m_logger;
    };

    TEST_F(LogHelperTest, InfoLevelDropsHighVolumeSuccessAndKeepsFailures) {
        m_logger->set_level(spdlog::level::info);

        Logger::HighVolumeDetail() << "chunk-detail";
        Logger::HighVolumeSuccess() << "chunk-success";
        Logger::HighVolumeFailure() << "chunk-failure";

        const auto output = Output();
        EXPECT_EQ(output.find("chunk-detail"), std::string::npos);
        EXPECT_EQ(output.find("chunk-success"), std::string::npos);
        EXPECT_NE(output.find("chunk-failure"), std::string::npos);
    }

    TEST_F(LogHelperTest, DebugLevelKeepsHighVolumeDetailsAndSuccesses) {
        m_logger->set_level(spdlog::level::debug);

        Logger::HighVolumeDetail() << "chunk-detail";
        Logger::HighVolumeSuccess() << "chunk-success";

        const auto output = Output();
        EXPECT_NE(output.find("chunk-detail"), std::string::npos);
        EXPECT_NE(output.find("chunk-success"), std::string::npos);
    }

} // namespace disk::utils::test
