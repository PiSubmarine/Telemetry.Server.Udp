#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

#include "PiSubmarine/Lease/Api/ILeaseValidator.h"
#include "PiSubmarine/Lease/Api/IResourceRegistry.h"
#include "PiSubmarine/Telemetry/Api/ISource.h"
#include "PiSubmarine/Time/ITickable.h"
#include "PiSubmarine/Udp/Api/ISender.h"

namespace PiSubmarine::Telemetry::Server::Udp
{
    class Server final : public Time::ITickable
    {
    public:
        Server(
            Api::ISource& source,
            Lease::Api::IResourceRegistry& resourceRegistry,
            const Lease::Api::ILeaseValidator& leaseValidator,
            ::PiSubmarine::Udp::Api::ISender& sender);

        [[nodiscard]] Error::Api::Result<void> Subscribe(
            const Lease::Api::LeaseId& leaseId,
            const ::PiSubmarine::Udp::Api::Endpoint& endpoint);

        void Tick(const std::chrono::nanoseconds& uptime, const std::chrono::nanoseconds& deltaTime) override;

    private:
        struct Subscriber
        {
            Lease::Api::LeaseId Lease;
            ::PiSubmarine::Udp::Api::Endpoint Endpoint;
        };

        [[nodiscard]] static Lease::Api::ResourceId MakeTelemetryResourceId();
        [[nodiscard]] static std::vector<std::byte> SerializeSnapshot(const Api::Snapshot& snapshot);

        Api::ISource& m_Source;
        Lease::Api::IResourceRegistry& m_ResourceRegistry;
        const Lease::Api::ILeaseValidator& m_LeaseValidator;
        ::PiSubmarine::Udp::Api::ISender& m_Sender;
        std::unordered_map<std::string, Subscriber> m_Subscribers;
    };
}
