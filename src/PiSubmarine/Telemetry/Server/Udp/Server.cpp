#include "PiSubmarine/Telemetry/Server/Udp/Server.h"

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace PiSubmarine::Telemetry::Server::Udp
{
    namespace
    {
        void AppendUInt32BigEndian(std::vector<std::byte>& bytes, const std::uint32_t value)
        {
            bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
            bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
            bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
            bytes.push_back(static_cast<std::byte>(value & 0xFFU));
        }
    }

    Server::Server(
        const Sources& sources,
        Lease::Api::IResourceRegistry& resourceRegistry,
        const Lease::Api::ILeaseValidator& leaseValidator,
        ::PiSubmarine::Udp::Api::IReceiver& receiver,
        ::PiSubmarine::Udp::Api::ISender& sender)
        : m_Sources(sources)
        , m_ResourceRegistry(resourceRegistry)
        , m_LeaseValidator(leaseValidator)
        , m_Receiver(receiver)
        , m_Sender(sender)
    {
        for (const auto& [channel, source] : m_Sources)
        {
            static_cast<void>(channel);
            if (source == nullptr)
            {
                throw std::invalid_argument("Telemetry channel source cannot be null");
            }
        }

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

    void Server::Tick(const std::chrono::nanoseconds& uptime, const std::chrono::nanoseconds& deltaTime)
    {
        static_cast<void>(uptime);
        static_cast<void>(deltaTime);

        while (true)
        {
            const auto receiveResult = m_Receiver.TryReceive();
            if (!receiveResult.has_value() || !receiveResult->has_value())
            {
                break;
            }

            HandleSubscriptionDatagram(receiveResult->value(), m_LeaseValidator, m_Subscribers);
        }

        const auto payload = BuildPayload();

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

    std::vector<std::byte> Server::BuildPayload() const
    {
        std::vector<std::pair<std::reference_wrapper<const Api::ChannelId>, std::vector<std::byte>>> availablePayloads;
        availablePayloads.reserve(m_Sources.size());

        for (const auto& [channel, source] : m_Sources)
        {
            const auto payloadResult = source->GetRaw();
            if (!payloadResult.has_value())
            {
                continue;
            }

            availablePayloads.emplace_back(std::cref(channel), *payloadResult);
        }

        std::vector<std::byte> bytes;
        AppendUInt32BigEndian(bytes, static_cast<std::uint32_t>(availablePayloads.size()));
        for (const auto& [channel, payload] : availablePayloads)
        {
            AppendUInt32BigEndian(bytes, static_cast<std::uint32_t>(channel.get().Value.size()));
            for (const char character : channel.get().Value)
            {
                bytes.push_back(static_cast<std::byte>(character));
            }

            AppendUInt32BigEndian(bytes, static_cast<std::uint32_t>(payload.size()));
            bytes.insert(bytes.end(), payload.begin(), payload.end());
        }

        return bytes;
    }

    void Server::HandleSubscriptionDatagram(
        const ::PiSubmarine::Udp::Api::Datagram& datagram,
        const Lease::Api::ILeaseValidator& leaseValidator,
        std::unordered_map<std::string, Subscriber>& subscribers)
    {
        Lease::Api::LeaseId leaseId;
        leaseId.Value.reserve(datagram.Payload.size());

        for (const auto byte : datagram.Payload)
        {
            leaseId.Value.push_back(std::to_integer<char>(byte));
        }

        if (leaseId.Value.empty())
        {
            return;
        }

        const auto validationResult = leaseValidator.ValidateLease(leaseId, MakeTelemetryResourceId());
        if (!validationResult.has_value() || !validationResult->IsValid)
        {
            return;
        }

        subscribers[leaseId.Value] = Subscriber{
            .Lease = std::move(leaseId),
            .Endpoint = datagram.Peer};
    }
}
