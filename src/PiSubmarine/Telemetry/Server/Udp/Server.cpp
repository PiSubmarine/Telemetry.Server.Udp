#include "PiSubmarine/Telemetry/Server/Udp/Server.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <span>
#include <vector>

#include "PiSubmarine/Error/Api/MakeError.h"

namespace PiSubmarine::Telemetry::Server::Udp
{
    namespace
    {
        constexpr std::size_t EncodedFieldSize = sizeof(std::uint32_t);

        void AppendUInt32BigEndian(std::vector<std::byte>& bytes, const std::uint32_t value)
        {
            bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
            bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
            bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
            bytes.push_back(static_cast<std::byte>(value & 0xFFU));
        }

        [[nodiscard]] Error::Api::Error MakeContractError()
        {
            return Error::Api::MakeError(Error::Api::ErrorCondition::ContractError);
        }

        [[nodiscard]] std::uint32_t ReadUInt32BigEndian(std::span<const std::byte> bytes, std::size_t offset)
        {
            return (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset])) << 24U)
                | (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 1])) << 16U)
                | (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 2])) << 8U)
                | static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 3]));
        }

        [[nodiscard]] std::vector<std::byte> EncodeString(const std::string& value)
        {
            std::vector<std::byte> bytes;
            bytes.reserve(value.size());
            for (const char character : value)
            {
                bytes.push_back(static_cast<std::byte>(character));
            }

            return bytes;
        }

        [[nodiscard]] Security::Aead::Api::Key MakeKey(const Lease::Api::LeaseSecret& leaseSecret)
        {
            return Security::Aead::Api::Key{.Value = leaseSecret.Value};
        }

        [[nodiscard]] Security::Aead::Api::AssociatedData MakeAssociatedData(const Lease::Api::LeaseId& leaseId)
        {
            return Security::Aead::Api::AssociatedData{.Value = EncodeString(leaseId.Value)};
        }

        struct Packet
        {
            Lease::Api::LeaseId LeaseId;
            Security::Api::Nonce Nonce;
            Security::Aead::Api::Ciphertext Ciphertext;
        };

        [[nodiscard]] Error::Api::Result<Packet> ParsePacket(std::span<const std::byte> bytes)
        {
            if (bytes.size() < EncodedFieldSize * 2)
            {
                return std::unexpected(MakeContractError());
            }

            std::size_t offset = 0;
            const auto leaseIdLength = ReadUInt32BigEndian(bytes, offset);
            offset += EncodedFieldSize;
            if (bytes.size() - offset < leaseIdLength + EncodedFieldSize)
            {
                return std::unexpected(MakeContractError());
            }

            Lease::Api::LeaseId leaseId;
            leaseId.Value.reserve(leaseIdLength);
            for (std::uint32_t index = 0; index < leaseIdLength; ++index)
            {
                leaseId.Value.push_back(static_cast<char>(std::to_integer<unsigned char>(bytes[offset + index])));
            }
            offset += leaseIdLength;

            const auto nonceLength = ReadUInt32BigEndian(bytes, offset);
            offset += EncodedFieldSize;
            if (bytes.size() - offset < nonceLength)
            {
                return std::unexpected(MakeContractError());
            }

            Security::Api::Nonce nonce;
            nonce.Value.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + nonceLength));
            offset += nonceLength;

            Security::Aead::Api::Ciphertext ciphertext;
            ciphertext.Value.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                bytes.end());

            return Packet{
                .LeaseId = std::move(leaseId),
                .Nonce = std::move(nonce),
                .Ciphertext = std::move(ciphertext)};
        }

        [[nodiscard]] std::vector<std::byte> BuildPacket(
            const Lease::Api::LeaseId& leaseId,
            const Security::Api::Nonce& nonce,
            const Security::Aead::Api::Ciphertext& ciphertext)
        {
            std::vector<std::byte> bytes;
            bytes.reserve(EncodedFieldSize * 2 + leaseId.Value.size() + nonce.Value.size() + ciphertext.Value.size());

            AppendUInt32BigEndian(bytes, static_cast<std::uint32_t>(leaseId.Value.size()));
            const auto leaseIdBytes = EncodeString(leaseId.Value);
            bytes.insert(bytes.end(), leaseIdBytes.begin(), leaseIdBytes.end());

            AppendUInt32BigEndian(bytes, static_cast<std::uint32_t>(nonce.Value.size()));
            bytes.insert(bytes.end(), nonce.Value.begin(), nonce.Value.end());
            bytes.insert(bytes.end(), ciphertext.Value.begin(), ciphertext.Value.end());

            return bytes;
        }
    }

    Server::Server(
        const Sources& sources,
        Lease::Api::IResourceRegistry& resourceRegistry,
        const Lease::Api::ILeaseSecretProvider& leaseSecretProvider,
        const Lease::Api::ILeaseValidator& leaseValidator,
        const ::PiSubmarine::Security::Aead::Api::IProvider& aeadProvider,
        ::PiSubmarine::Security::Api::INonceProvider& nonceProvider,
        ::PiSubmarine::Udp::Api::IReceiver& receiver,
        ::PiSubmarine::Udp::Api::ISender& sender)
        : m_Sources(sources)
        , m_ResourceRegistry(resourceRegistry)
        , m_LeaseSecretProvider(leaseSecretProvider)
        , m_LeaseValidator(leaseValidator)
        , m_AeadProvider(aeadProvider)
        , m_NonceProvider(nonceProvider)
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

            HandleSubscriptionDatagram(receiveResult->value(), m_Subscribers);
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

            const auto secretResult = m_LeaseSecretProvider.GetLeaseSecret(iterator->second.Lease);
            if (!secretResult.has_value())
            {
                iterator = m_Subscribers.erase(iterator);
                continue;
            }

            const auto nonceResult = m_NonceProvider.Next();
            if (!nonceResult.has_value())
            {
                ++iterator;
                continue;
            }

            const auto ciphertextResult = m_AeadProvider.Seal(
                MakeKey(*secretResult),
                *nonceResult,
                Security::Aead::Api::Plaintext{.Value = payload},
                MakeAssociatedData(iterator->second.Lease));
            if (!ciphertextResult.has_value())
            {
                ++iterator;
                continue;
            }

            const ::PiSubmarine::Udp::Api::Datagram datagram{
                .Peer = iterator->second.Endpoint,
                .Payload = BuildPacket(iterator->second.Lease, *nonceResult, *ciphertextResult)};

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

    void Server::HandleSubscriptionDatagram(const ::PiSubmarine::Udp::Api::Datagram& datagram, std::unordered_map<std::string, Subscriber>& subscribers)
    {
        const auto packetResult = ParsePacket(datagram.Payload);
        if (!packetResult.has_value() || packetResult->LeaseId.Value.empty())
        {
            return;
        }

        const auto validationResult = m_LeaseValidator.ValidateLease(packetResult->LeaseId, MakeTelemetryResourceId());
        if (!validationResult.has_value() || !validationResult->IsValid)
        {
            return;
        }

        const auto secretResult = m_LeaseSecretProvider.GetLeaseSecret(packetResult->LeaseId);
        if (!secretResult.has_value())
        {
            return;
        }

        const auto plaintextResult = m_AeadProvider.Open(
            MakeKey(*secretResult),
            packetResult->Nonce,
            packetResult->Ciphertext,
            MakeAssociatedData(packetResult->LeaseId));
        if (!plaintextResult.has_value() || !plaintextResult->Value.empty())
        {
            return;
        }

        subscribers[packetResult->LeaseId.Value] = Subscriber{
            .Lease = std::move(packetResult->LeaseId),
            .Endpoint = datagram.Peer};
    }
}
