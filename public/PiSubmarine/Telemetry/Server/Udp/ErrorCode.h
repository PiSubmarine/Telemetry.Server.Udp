#pragma once

#include <system_error>

namespace PiSubmarine::Telemetry::Server::Udp
{
    enum class ErrorCode
    {
        InvalidTelemetryLease = 1
    };

    [[nodiscard]] std::error_code make_error_code(ErrorCode errorCode) noexcept;
}

namespace std
{
    template<>
    struct is_error_code_enum<PiSubmarine::Telemetry::Server::Udp::ErrorCode> : true_type
    {
    };
}
