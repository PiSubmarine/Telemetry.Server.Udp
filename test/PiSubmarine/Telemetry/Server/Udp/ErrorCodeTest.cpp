#include <gtest/gtest.h>

#include "PiSubmarine/Telemetry/Server/Udp/ErrorCode.h"

namespace PiSubmarine::Telemetry::Server::Udp
{
    TEST(ErrorCodeTest, ConvertsToErrorCode)
    {
        const auto errorCode = make_error_code(ErrorCode::InvalidTelemetryLease);

        EXPECT_EQ(errorCode.value(), static_cast<int>(ErrorCode::InvalidTelemetryLease));
        EXPECT_STREQ(errorCode.category().name(), "PiSubmarine.Telemetry.Server.Udp");
    }
}
