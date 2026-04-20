#include "PiSubmarine/Telemetry/Server/Udp/Server.h"

#include <sstream>
#include <string>
#include <vector>

#include "PiSubmarine/Error/Api/MakeError.h"
#include "PiSubmarine/Telemetry/Server/Udp/ErrorCode.h"

namespace PiSubmarine::Telemetry::Server::Udp
{
    Server::Server(
        Api::ISource& source,
        Lease::Api::IResourceRegistry& resourceRegistry,
        const Lease::Api::ILeaseValidator& leaseValidator,
        ::PiSubmarine::Udp::Api::ISender& sender)
        : m_Source(source)
        , m_ResourceRegistry(resourceRegistry)
        , m_LeaseValidator(leaseValidator)
        , m_Sender(sender)
    {
        const Lease::Api::ResourceDescriptor resourceDescriptor{
            .Id = MakeTelemetryResourceId(),
            .Policy = Lease::Api::LeasePolicy{
                .MaxLeases = std::nullopt,
                .LeaseDuration = std::chrono::milliseconds(3000)}};

        const auto registerResult = m_ResourceRegistry.RegisterResource(resourceDescriptor);
        if (!registerResult.has_value())
        {
            throw std::runtime_error("Failed to register telemetry resource");
        }
    }

    Error::Api::Result<void> Server::Subscribe(
        const Lease::Api::LeaseId& leaseId,
        const ::PiSubmarine::Udp::Api::Endpoint& endpoint)
    {
        const auto validationResult = m_LeaseValidator.ValidateLease(leaseId, MakeTelemetryResourceId());
        if (!validationResult.has_value())
        {
            return std::unexpected(validationResult.error());
        }

        if (!validationResult->IsValid)
        {
            return std::unexpected(Error::Api::MakeError(
                Error::Api::ErrorCondition::ContractError,
                make_error_code(ErrorCode::InvalidTelemetryLease)));
        }

        m_Subscribers[leaseId.Value] = Subscriber{
            .Lease = leaseId,
            .Endpoint = endpoint};

        return {};
    }

    void Server::Tick(const std::chrono::nanoseconds& uptime, const std::chrono::nanoseconds& deltaTime)
    {
        static_cast<void>(uptime);
        static_cast<void>(deltaTime);

        const auto snapshot = m_Source.GetSnapshot();
        const auto payload = SerializeSnapshot(snapshot);

        for (auto iterator = m_Subscribers.begin(); iterator != m_Subscribers.end();)
        {
            const auto validationResult = m_LeaseValidator.ValidateLease(
                iterator->second.Lease,
                MakeTelemetryResourceId());

            if (!validationResult.has_value() || !validationResult->IsValid)
            {
                iterator = m_Subscribers.erase(iterator);
                continue;
            }

            const ::PiSubmarine::Udp::Api::Datagram datagram{
                .Peer = iterator->second.Endpoint,
                .Payload = payload};

            const auto sendResult = m_Sender.Send(datagram);
            static_cast<void>(sendResult);
            ++iterator;
        }
    }

    Lease::Api::ResourceId Server::MakeTelemetryResourceId()
    {
        return Lease::Api::ResourceId{.Value = "telemetry-main"};
    }

    std::vector<std::byte> Server::SerializeSnapshot(const Api::Snapshot& snapshot)
    {
        std::ostringstream stream;

        stream << "depth=";
        if (snapshot.Depth.has_value())
        {
            stream << snapshot.Depth->Value;
        }
        else
        {
            stream << "none";
        }

        stream << ";distance_to_seafloor=";
        if (snapshot.DistanceToSeaFloor.has_value())
        {
            stream << snapshot.DistanceToSeaFloor->Value;
        }
        else
        {
            stream << "none";
        }

        stream << ";battery_soc=";
        if (snapshot.BatteryState.has_value())
        {
            stream << static_cast<double>(snapshot.BatteryState->StateOfCharge);
        }
        else
        {
            stream << "none";
        }

        stream << ";ballast=";
        if (snapshot.BallastPosition.has_value())
        {
            stream << static_cast<double>(*snapshot.BallastPosition);
        }
        else
        {
            stream << "none";
        }

        stream << ";thrusters=";
        for (std::size_t index = 0; index < snapshot.Thrusters.size(); ++index)
        {
            if (index != 0)
            {
                stream << ",";
            }

            stream << static_cast<int>(snapshot.Thrusters[index].Operational);
        }

        const auto stringPayload = stream.str();
        std::vector<std::byte> payload;
        payload.reserve(stringPayload.size());

        for (const char character : stringPayload)
        {
            payload.push_back(static_cast<std::byte>(character));
        }

        return payload;
    }
}
