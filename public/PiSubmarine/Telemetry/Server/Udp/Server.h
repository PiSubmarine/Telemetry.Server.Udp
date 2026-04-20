#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

#include "PiSubmarine/Lease/Api/ILeaseValidator.h"
#include "PiSubmarine/Lease/Api/IResourceRegistry.h"
#include "PiSubmarine/Telemetry/Api/ISource.h"
#include "PiSubmarine/Telemetry/ISerializer.h"
#include "PiSubmarine/Time/ITickable.h"
#include "PiSubmarine/Udp/Api/IReceiver.h"
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
            const ::PiSubmarine::Telemetry::ISerializer& serializer,
            ::PiSubmarine::Udp::Api::IReceiver& receiver,
            ::PiSubmarine::Udp::Api::ISender& sender);

        void Tick(const std::chrono::nanoseconds& uptime, const std::chrono::nanoseconds& deltaTime) override;

    private:
        struct Subscriber
        {
            Lease::Api::LeaseId Lease;
            ::PiSubmarine::Udp::Api::Endpoint Endpoint;
        };

        [[nodiscard]] static Lease::Api::ResourceId MakeTelemetryResourceId();
        static void HandleSubscriptionDatagram(
            const ::PiSubmarine::Udp::Api::Datagram& datagram,
            const Lease::Api::ILeaseValidator& leaseValidator,
            std::unordered_map<std::string, Subscriber>& subscribers);

        Api::ISource& m_Source;
        Lease::Api::IResourceRegistry& m_ResourceRegistry;
        const Lease::Api::ILeaseValidator& m_LeaseValidator;
        const ::PiSubmarine::Telemetry::ISerializer& m_Serializer;
        ::PiSubmarine::Udp::Api::IReceiver& m_Receiver;
        ::PiSubmarine::Udp::Api::ISender& m_Sender;
        std::unordered_map<std::string, Subscriber> m_Subscribers;
    };
}
