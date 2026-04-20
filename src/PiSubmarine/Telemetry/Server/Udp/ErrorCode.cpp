#include "PiSubmarine/Telemetry/Server/Udp/ErrorCode.h"

#include <array>
#include <string_view>

namespace PiSubmarine::Telemetry::Server::Udp
{
    namespace
    {
        class ErrorCategory final : public std::error_category
        {
        public:
            [[nodiscard]] const char* name() const noexcept override
            {
                return "PiSubmarine.Telemetry.Server.Udp";
            }

            [[nodiscard]] std::string message(const int condition) const override
            {
                constexpr std::array<std::string_view, 2> Messages{
                    "success",
                    "invalid telemetry lease"};

                const auto index = static_cast<std::size_t>(condition);
                if (index >= Messages.size())
                {
                    return "unknown telemetry UDP server error";
                }

                return std::string(Messages[index]);
            }
        };
    }

    [[nodiscard]] std::error_code make_error_code(const ErrorCode errorCode) noexcept
    {
        static const ErrorCategory Category;
        return {static_cast<int>(errorCode), Category};
    }
}
