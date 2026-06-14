#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "PiSubmarine/Error/Api/MakeError.h"
#include "PiSubmarine/Lease/Api/ILeaseSecretProviderMock.h"
#include "PiSubmarine/Lease/Api/ILeaseValidatorMock.h"
#include "PiSubmarine/Lease/Api/IResourceRegistryMock.h"
#include "PiSubmarine/Security/Api/INonceProviderMock.h"
#include "PiSubmarine/Security/Aead/Api/IProviderMock.h"
#include "PiSubmarine/Telemetry/Api/IRawSourceMock.h"
#include "PiSubmarine/Telemetry/Server/Udp/Server.h"
#include "PiSubmarine/Udp/Api/IReceiverMock.h"
#include "PiSubmarine/Udp/Api/ISenderMock.h"

namespace PiSubmarine::Telemetry::Server::Udp
{
    namespace
    {
        using ::testing::_;
        using ::testing::Return;
        using ::testing::StrictMock;

        constexpr std::size_t EncodedFieldSize = sizeof(std::uint32_t);

        void AppendUInt32BigEndian(std::vector<std::byte>& bytes, const std::uint32_t value)
        {
            bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
            bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
            bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
            bytes.push_back(static_cast<std::byte>(value & 0xFFU));
        }

        [[nodiscard]] std::vector<std::byte> EncodeDatagram(
            const std::initializer_list<std::pair<std::string_view, std::vector<std::byte>>> channels)
        {
            std::vector<std::byte> bytes;
            AppendUInt32BigEndian(bytes, static_cast<std::uint32_t>(channels.size()));
            for (const auto& [channel, payload] : channels)
            {
                AppendUInt32BigEndian(bytes, static_cast<std::uint32_t>(channel.size()));
                for (const char character : channel)
                {
                    bytes.push_back(static_cast<std::byte>(character));
                }

                AppendUInt32BigEndian(bytes, static_cast<std::uint32_t>(payload.size()));
                bytes.insert(bytes.end(), payload.begin(), payload.end());
            }

            return bytes;
        }

        [[nodiscard]] std::vector<std::byte> EncodePacket(
            std::string_view leaseId,
            const std::vector<std::byte>& nonce,
            const std::vector<std::byte>& ciphertext)
        {
            std::vector<std::byte> bytes;
            bytes.reserve(EncodedFieldSize * 2 + leaseId.size() + nonce.size() + ciphertext.size());
            AppendUInt32BigEndian(bytes, static_cast<std::uint32_t>(leaseId.size()));
            for (const char character : leaseId)
            {
                bytes.push_back(static_cast<std::byte>(character));
            }

            AppendUInt32BigEndian(bytes, static_cast<std::uint32_t>(nonce.size()));
            bytes.insert(bytes.end(), nonce.begin(), nonce.end());
            bytes.insert(bytes.end(), ciphertext.begin(), ciphertext.end());
            return bytes;
        }

        [[nodiscard]] Lease::Api::LeaseSecret MakeLeaseSecret()
        {
            return Lease::Api::LeaseSecret{
                .Value = {
                    std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                    std::byte{0x04}, std::byte{0x05}, std::byte{0x06}, std::byte{0x07},
                    std::byte{0x08}, std::byte{0x09}, std::byte{0x0A}, std::byte{0x0B},
                    std::byte{0x0C}, std::byte{0x0D}, std::byte{0x0E}, std::byte{0x0F},
                    std::byte{0x10}, std::byte{0x11}, std::byte{0x12}, std::byte{0x13},
                    std::byte{0x14}, std::byte{0x15}, std::byte{0x16}, std::byte{0x17},
                    std::byte{0x18}, std::byte{0x19}, std::byte{0x1A}, std::byte{0x1B},
                    std::byte{0x1C}, std::byte{0x1D}, std::byte{0x1E}, std::byte{0x1F}}};
        }

        [[nodiscard]] Security::Aead::Api::Key MakeKey()
        {
            return Security::Aead::Api::Key{.Value = MakeLeaseSecret().Value};
        }

        [[nodiscard]] Security::Aead::Api::AssociatedData MakeAssociatedData()
        {
            return Security::Aead::Api::AssociatedData{
                .Value = {
                    std::byte{'l'}, std::byte{'e'}, std::byte{'a'},
                    std::byte{'s'}, std::byte{'e'}, std::byte{'-'}, std::byte{'1'}}};
        }
    }

    TEST(ServerTest, RegistersTelemetryResourceOnConstruction)
    {
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseSecretProviderMock> leaseSecretProvider;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<Security::Aead::Api::IProviderMock> aeadProvider;
        StrictMock<Security::Api::INonceProviderMock> nonceProvider;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources;

        EXPECT_CALL(resourceRegistry, RegisterResource(Lease::Api::ResourceDescriptor{
                        .Id = Lease::Api::ResourceId{.Value = "telemetry-main"},
                        .Policy = Lease::Api::LeasePolicy{
                            .MaxLeases = std::nullopt,
                            .LeaseDuration = std::chrono::milliseconds(3000)}}))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseSecretProvider, leaseValidator, aeadProvider, nonceProvider, receiver, sender);
    }

    TEST(ServerTest, TickSendsAuthenticatedChannelPayloadsToValidSubscribers)
    {
        StrictMock<Api::IRawSourceMock> batterySource;
        StrictMock<Api::IRawSourceMock> motorSource;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseSecretProviderMock> leaseSecretProvider;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<Security::Aead::Api::IProviderMock> aeadProvider;
        StrictMock<Security::Api::INonceProviderMock> nonceProvider;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources{
            {Api::ChannelId{.Value = "battery.main"}, &batterySource},
            {Api::ChannelId{.Value = "motor.front-left"}, &motorSource}};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseSecretProvider, leaseValidator, aeadProvider, nonceProvider, receiver, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})));
        EXPECT_CALL(leaseSecretProvider, GetLeaseSecret(Lease::Api::LeaseId{.Value = "lease-1"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseSecret>(MakeLeaseSecret())))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseSecret>(MakeLeaseSecret())));

        EXPECT_CALL(receiver, TryReceive())
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000},
                    .Payload = EncodePacket("lease-1", {std::byte{0x01}}, {std::byte{0x02}})})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                std::optional<::PiSubmarine::Udp::Api::Datagram>{std::nullopt})));
        EXPECT_CALL(aeadProvider, Open(
                        MakeKey(),
                        Security::Api::Nonce{.Value = {std::byte{0x01}}},
                        Security::Aead::Api::Ciphertext{.Value = {std::byte{0x02}}},
                        MakeAssociatedData()))
            .WillOnce(Return(Error::Api::Result<Security::Aead::Api::Plaintext>(Security::Aead::Api::Plaintext{})));

        EXPECT_CALL(batterySource, GetRaw())
            .WillOnce(Return(Error::Api::Result<std::vector<std::byte>>(
                std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}})));
        EXPECT_CALL(motorSource, GetRaw())
            .WillOnce(Return(Error::Api::Result<std::vector<std::byte>>(
                std::vector<std::byte>{std::byte{0x03}})));
        EXPECT_CALL(nonceProvider, Next())
            .WillOnce(Return(Error::Api::Result<Security::Api::Nonce>(
                Security::Api::Nonce{.Value = {std::byte{0xAA}}})));
        EXPECT_CALL(aeadProvider, Seal(
                        MakeKey(),
                        Security::Api::Nonce{.Value = {std::byte{0xAA}}},
                        Security::Aead::Api::Plaintext{
                            .Value = EncodeDatagram({
                                {"battery.main", {std::byte{0x01}, std::byte{0x02}}},
                                {"motor.front-left", {std::byte{0x03}}}})},
                        MakeAssociatedData()))
            .WillOnce(Return(Error::Api::Result<Security::Aead::Api::Ciphertext>(
                Security::Aead::Api::Ciphertext{.Value = {std::byte{0xBB}}})));
        EXPECT_CALL(sender, Send(testing::Truly([](const ::PiSubmarine::Udp::Api::Datagram& datagram)
            {
                return datagram.Peer == ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000}
                    && datagram.Payload == EncodePacket("lease-1", {std::byte{0xAA}}, {std::byte{0xBB}});
            })))
            .WillOnce(Return(Error::Api::Result<void>{}));

        server.Tick(
            std::chrono::nanoseconds(std::chrono::milliseconds(100)),
            std::chrono::nanoseconds(std::chrono::milliseconds(10)));
    }

    TEST(ServerTest, TickSkipsUnavailableChannelsButStillSendsRemainingPayloads)
    {
        StrictMock<Api::IRawSourceMock> batterySource;
        StrictMock<Api::IRawSourceMock> motorSource;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseSecretProviderMock> leaseSecretProvider;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<Security::Aead::Api::IProviderMock> aeadProvider;
        StrictMock<Security::Api::INonceProviderMock> nonceProvider;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources{
            {Api::ChannelId{.Value = "battery.main"}, &batterySource},
            {Api::ChannelId{.Value = "motor.front-left"}, &motorSource}};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseSecretProvider, leaseValidator, aeadProvider, nonceProvider, receiver, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})));
        EXPECT_CALL(leaseSecretProvider, GetLeaseSecret(Lease::Api::LeaseId{.Value = "lease-1"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseSecret>(MakeLeaseSecret())))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseSecret>(MakeLeaseSecret())));
        EXPECT_CALL(receiver, TryReceive())
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000},
                    .Payload = EncodePacket("lease-1", {std::byte{0x01}}, {std::byte{0x02}})})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                std::optional<::PiSubmarine::Udp::Api::Datagram>{std::nullopt})));
        EXPECT_CALL(aeadProvider, Open(_, _, _, _))
            .WillOnce(Return(Error::Api::Result<Security::Aead::Api::Plaintext>(Security::Aead::Api::Plaintext{})));

        EXPECT_CALL(batterySource, GetRaw())
            .WillOnce(Return(std::unexpected(Error::Api::MakeError(Error::Api::ErrorCondition::CommunicationError))));
        EXPECT_CALL(motorSource, GetRaw())
            .WillOnce(Return(Error::Api::Result<std::vector<std::byte>>(
                std::vector<std::byte>{std::byte{0x03}})));
        EXPECT_CALL(nonceProvider, Next())
            .WillOnce(Return(Error::Api::Result<Security::Api::Nonce>(
                Security::Api::Nonce{.Value = {std::byte{0xAB}}})));
        EXPECT_CALL(aeadProvider, Seal(
                        _,
                        Security::Api::Nonce{.Value = {std::byte{0xAB}}},
                        Security::Aead::Api::Plaintext{
                            .Value = EncodeDatagram({
                                {"motor.front-left", {std::byte{0x03}}}})},
                        _))
            .WillOnce(Return(Error::Api::Result<Security::Aead::Api::Ciphertext>(
                Security::Aead::Api::Ciphertext{.Value = {std::byte{0xBC}}})));
        EXPECT_CALL(sender, Send(testing::Truly([](const ::PiSubmarine::Udp::Api::Datagram& datagram)
            {
                return datagram.Payload == EncodePacket("lease-1", {std::byte{0xAB}}, {std::byte{0xBC}});
            })))
            .WillOnce(Return(Error::Api::Result<void>{}));

        server.Tick(
            std::chrono::nanoseconds(std::chrono::milliseconds(100)),
            std::chrono::nanoseconds(std::chrono::milliseconds(10)));
    }

    TEST(ServerTest, SubscribeReplacesStoredEndpointForSameLease)
    {
        StrictMock<Api::IRawSourceMock> batterySource;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseSecretProviderMock> leaseSecretProvider;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<Security::Aead::Api::IProviderMock> aeadProvider;
        StrictMock<Security::Api::INonceProviderMock> nonceProvider;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources{{Api::ChannelId{.Value = "battery.main"}, &batterySource}};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseSecretProvider, leaseValidator, aeadProvider, nonceProvider, receiver, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})));
        EXPECT_CALL(leaseSecretProvider, GetLeaseSecret(Lease::Api::LeaseId{.Value = "lease-1"}))
            .Times(3)
            .WillRepeatedly(Return(Error::Api::Result<Lease::Api::LeaseSecret>(MakeLeaseSecret())));
        EXPECT_CALL(receiver, TryReceive())
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000},
                    .Payload = EncodePacket("lease-1", {std::byte{0x01}}, {std::byte{0x02}})})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9001},
                    .Payload = EncodePacket("lease-1", {std::byte{0x03}}, {std::byte{0x04}})})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                std::optional<::PiSubmarine::Udp::Api::Datagram>{std::nullopt})));
        EXPECT_CALL(aeadProvider, Open(_, _, _, _))
            .Times(2)
            .WillRepeatedly(Return(Error::Api::Result<Security::Aead::Api::Plaintext>(Security::Aead::Api::Plaintext{})));

        EXPECT_CALL(batterySource, GetRaw())
            .WillOnce(Return(Error::Api::Result<std::vector<std::byte>>(
                std::vector<std::byte>{std::byte{0x03}})));
        EXPECT_CALL(nonceProvider, Next())
            .WillOnce(Return(Error::Api::Result<Security::Api::Nonce>(
                Security::Api::Nonce{.Value = {std::byte{0x05}}})));
        EXPECT_CALL(aeadProvider, Seal(_, _, _, _))
            .WillOnce(Return(Error::Api::Result<Security::Aead::Api::Ciphertext>(
                Security::Aead::Api::Ciphertext{.Value = {std::byte{0x06}}})));
        EXPECT_CALL(sender, Send(testing::Truly([](const ::PiSubmarine::Udp::Api::Datagram& datagram)
            {
                return datagram.Peer == ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9001};
            })))
            .WillOnce(Return(Error::Api::Result<void>{}));

        server.Tick(
            std::chrono::nanoseconds(std::chrono::milliseconds(100)),
            std::chrono::nanoseconds(std::chrono::milliseconds(10)));
    }

    TEST(ServerTest, TickRemovesSubscribersWithExpiredLeases)
    {
        StrictMock<Api::IRawSourceMock> batterySource;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseSecretProviderMock> leaseSecretProvider;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<Security::Aead::Api::IProviderMock> aeadProvider;
        StrictMock<Security::Api::INonceProviderMock> nonceProvider;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources{{Api::ChannelId{.Value = "battery.main"}, &batterySource}};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseSecretProvider, leaseValidator, aeadProvider, nonceProvider, receiver, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = false})));
        EXPECT_CALL(leaseSecretProvider, GetLeaseSecret(Lease::Api::LeaseId{.Value = "lease-1"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseSecret>(MakeLeaseSecret())));
        EXPECT_CALL(receiver, TryReceive())
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000},
                    .Payload = EncodePacket("lease-1", {std::byte{0x01}}, {std::byte{0x02}})})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                std::optional<::PiSubmarine::Udp::Api::Datagram>{std::nullopt})));
        EXPECT_CALL(aeadProvider, Open(_, _, _, _))
            .WillOnce(Return(Error::Api::Result<Security::Aead::Api::Plaintext>(Security::Aead::Api::Plaintext{})));
        EXPECT_CALL(batterySource, GetRaw())
            .WillOnce(Return(Error::Api::Result<std::vector<std::byte>>(
                std::vector<std::byte>{std::byte{0x04}})));

        server.Tick(
            std::chrono::nanoseconds(std::chrono::milliseconds(100)),
            std::chrono::nanoseconds(std::chrono::milliseconds(10)));
    }

    TEST(ServerTest, TickIgnoresInvalidSubscriptionLease)
    {
        StrictMock<Api::IRawSourceMock> batterySource;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseSecretProviderMock> leaseSecretProvider;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<Security::Aead::Api::IProviderMock> aeadProvider;
        StrictMock<Security::Api::INonceProviderMock> nonceProvider;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources{{Api::ChannelId{.Value = "battery.main"}, &batterySource}};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseSecretProvider, leaseValidator, aeadProvider, nonceProvider, receiver, sender);

        EXPECT_CALL(receiver, TryReceive())
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000},
                    .Payload = EncodePacket("lease-1", {std::byte{0x01}}, {std::byte{0x02}})})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                std::optional<::PiSubmarine::Udp::Api::Datagram>{std::nullopt})));
        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = false})));
        EXPECT_CALL(batterySource, GetRaw())
            .WillOnce(Return(Error::Api::Result<std::vector<std::byte>>(
                std::vector<std::byte>{std::byte{0x05}})));

        server.Tick(
            std::chrono::nanoseconds(std::chrono::milliseconds(100)),
            std::chrono::nanoseconds(std::chrono::milliseconds(10)));
    }

    TEST(ServerTest, TickStillSendsEmptyAuthenticatedDatagramWhenNoChannelPayloadIsAvailable)
    {
        StrictMock<Api::IRawSourceMock> batterySource;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseSecretProviderMock> leaseSecretProvider;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<Security::Aead::Api::IProviderMock> aeadProvider;
        StrictMock<Security::Api::INonceProviderMock> nonceProvider;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources{{Api::ChannelId{.Value = "battery.main"}, &batterySource}};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseSecretProvider, leaseValidator, aeadProvider, nonceProvider, receiver, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})));
        EXPECT_CALL(leaseSecretProvider, GetLeaseSecret(Lease::Api::LeaseId{.Value = "lease-1"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseSecret>(MakeLeaseSecret())))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseSecret>(MakeLeaseSecret())));
        EXPECT_CALL(receiver, TryReceive())
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000},
                    .Payload = EncodePacket("lease-1", {std::byte{0x01}}, {std::byte{0x02}})})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                std::optional<::PiSubmarine::Udp::Api::Datagram>{std::nullopt})));
        EXPECT_CALL(aeadProvider, Open(_, _, _, _))
            .WillOnce(Return(Error::Api::Result<Security::Aead::Api::Plaintext>(Security::Aead::Api::Plaintext{})));
        EXPECT_CALL(batterySource, GetRaw())
            .WillOnce(Return(std::unexpected(Error::Api::MakeError(Error::Api::ErrorCondition::CommunicationError))));
        EXPECT_CALL(nonceProvider, Next())
            .WillOnce(Return(Error::Api::Result<Security::Api::Nonce>(
                Security::Api::Nonce{.Value = {std::byte{0xFE}}})));
        EXPECT_CALL(aeadProvider, Seal(
                        _,
                        Security::Api::Nonce{.Value = {std::byte{0xFE}}},
                        Security::Aead::Api::Plaintext{.Value = EncodeDatagram({})},
                        _))
            .WillOnce(Return(Error::Api::Result<Security::Aead::Api::Ciphertext>(
                Security::Aead::Api::Ciphertext{.Value = {std::byte{0xFF}}})));
        EXPECT_CALL(sender, Send(testing::Truly([](const ::PiSubmarine::Udp::Api::Datagram& datagram)
            {
                return datagram.Payload == EncodePacket("lease-1", {std::byte{0xFE}}, {std::byte{0xFF}});
            })))
            .WillOnce(Return(Error::Api::Result<void>{}));

        server.Tick(
            std::chrono::nanoseconds(std::chrono::milliseconds(100)),
            std::chrono::nanoseconds(std::chrono::milliseconds(10)));
    }
}
