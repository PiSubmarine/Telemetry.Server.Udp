#pragma once

#include <cstddef>
#include <chrono>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "PiSubmarine/Lease/Api/ILeaseValidator.h"
#include "PiSubmarine/Lease/Api/IResourceRegistry.h"
#include "PiSubmarine/Telemetry/Api/ChannelId.h"
#include "PiSubmarine/Telemetry/Api/IRawSource.h"
#include "PiSubmarine/Time/ITickable.h"
#include "PiSubmarine/Udp/Api/IReceiver.h"
#include "PiSubmarine/Udp/Api/ISender.h"

namespace PiSubmarine::Telemetry::Server::Udp
{
    class Server final : public Time::ITickable
    {
    public:
        using Sources = std::map<Api::ChannelId, const Api::IRawSource*>;

        Server(
            const Sources& sources,
            Lease::Api::IResourceRegistry& resourceRegistry,
            const Lease::Api::ILeaseValidator& leaseValidator,
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
        [[nodiscard]] std::vector<std::byte> BuildPayload() const;
        static void HandleSubscriptionDatagram(
            const ::PiSubmarine::Udp::Api::Datagram& datagram,
            const Lease::Api::ILeaseValidator& leaseValidator,
            std::unordered_map<std::string, Subscriber>& subscribers);

        Sources m_Sources;
        Lease::Api::IResourceRegistry& m_ResourceRegistry;
        const Lease::Api::ILeaseValidator& m_LeaseValidator;
        ::PiSubmarine::Udp::Api::IReceiver& m_Receiver;
        ::PiSubmarine::Udp::Api::ISender& m_Sender;
        std::unordered_map<std::string, Subscriber> m_Subscribers;
    };
}
