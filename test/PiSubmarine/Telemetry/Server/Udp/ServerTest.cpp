#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string_view>

#include "PiSubmarine/Error/Api/MakeError.h"
#include "PiSubmarine/Lease/Api/ILeaseValidatorMock.h"
#include "PiSubmarine/Lease/Api/IResourceRegistryMock.h"
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

        [[nodiscard]] std::vector<std::byte> EncodeDatagram(
            const std::initializer_list<std::pair<std::string_view, std::vector<std::byte>>> channels)
        {
            std::vector<std::byte> bytes;
            const auto appendUInt32 = [&bytes](const std::uint32_t value)
            {
                bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
                bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
                bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
                bytes.push_back(static_cast<std::byte>(value & 0xFFU));
            };

            appendUInt32(static_cast<std::uint32_t>(channels.size()));
            for (const auto& [channel, payload] : channels)
            {
                appendUInt32(static_cast<std::uint32_t>(channel.size()));
                for (const char character : channel)
                {
                    bytes.push_back(static_cast<std::byte>(character));
                }

                appendUInt32(static_cast<std::uint32_t>(payload.size()));
                bytes.insert(bytes.end(), payload.begin(), payload.end());
            }

            return bytes;
        }
    }

    TEST(ServerTest, RegistersTelemetryResourceOnConstruction)
    {
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources;

        EXPECT_CALL(resourceRegistry, RegisterResource(Lease::Api::ResourceDescriptor{
                        .Id = Lease::Api::ResourceId{.Value = "telemetry-main"},
                        .Policy = Lease::Api::LeasePolicy{
                            .MaxLeases = std::nullopt,
                            .LeaseDuration = std::chrono::milliseconds(3000)}}))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseValidator, receiver, sender);
    }

    TEST(ServerTest, TickSendsAvailableChannelPayloadsToValidSubscribers)
    {
        StrictMock<Api::IRawSourceMock> batterySource;
        StrictMock<Api::IRawSourceMock> motorSource;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources{
            {Api::ChannelId{.Value = "battery.main"}, &batterySource},
            {Api::ChannelId{.Value = "motor.front-left"}, &motorSource}};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseValidator, receiver, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})));

        EXPECT_CALL(receiver, TryReceive())
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000},
                    .Payload = {
                        std::byte{'l'}, std::byte{'e'}, std::byte{'a'},
                        std::byte{'s'}, std::byte{'e'}, std::byte{'-'}, std::byte{'1'}}})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                std::optional<::PiSubmarine::Udp::Api::Datagram>{std::nullopt})));

        EXPECT_CALL(batterySource, GetRaw())
            .WillOnce(Return(Error::Api::Result<std::vector<std::byte>>(
                std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}})));
        EXPECT_CALL(motorSource, GetRaw())
            .WillOnce(Return(Error::Api::Result<std::vector<std::byte>>(
                std::vector<std::byte>{std::byte{0x03}})));
        EXPECT_CALL(sender, Send(testing::Truly([](const ::PiSubmarine::Udp::Api::Datagram& datagram)
            {
                return datagram.Peer == ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000}
                    && datagram.Payload == EncodeDatagram({
                        {"battery.main", {std::byte{0x01}, std::byte{0x02}}},
                        {"motor.front-left", {std::byte{0x03}}}});
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
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources{
            {Api::ChannelId{.Value = "battery.main"}, &batterySource},
            {Api::ChannelId{.Value = "motor.front-left"}, &motorSource}};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseValidator, receiver, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})));

        EXPECT_CALL(receiver, TryReceive())
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000},
                    .Payload = {
                        std::byte{'l'}, std::byte{'e'}, std::byte{'a'},
                        std::byte{'s'}, std::byte{'e'}, std::byte{'-'}, std::byte{'1'}}})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                std::optional<::PiSubmarine::Udp::Api::Datagram>{std::nullopt})));

        EXPECT_CALL(batterySource, GetRaw())
            .WillOnce(Return(std::unexpected(Error::Api::MakeError(Error::Api::ErrorCondition::CommunicationError))));
        EXPECT_CALL(motorSource, GetRaw())
            .WillOnce(Return(Error::Api::Result<std::vector<std::byte>>(
                std::vector<std::byte>{std::byte{0x03}})));
        EXPECT_CALL(sender, Send(testing::Truly([](const ::PiSubmarine::Udp::Api::Datagram& datagram)
            {
                return datagram.Payload == EncodeDatagram({
                    {"motor.front-left", {std::byte{0x03}}}});
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
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources{{Api::ChannelId{.Value = "battery.main"}, &batterySource}};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseValidator, receiver, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})));

        EXPECT_CALL(receiver, TryReceive())
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000},
                    .Payload = {
                        std::byte{'l'}, std::byte{'e'}, std::byte{'a'},
                        std::byte{'s'}, std::byte{'e'}, std::byte{'-'}, std::byte{'1'}}})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9001},
                    .Payload = {
                        std::byte{'l'}, std::byte{'e'}, std::byte{'a'},
                        std::byte{'s'}, std::byte{'e'}, std::byte{'-'}, std::byte{'1'}}})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                std::optional<::PiSubmarine::Udp::Api::Datagram>{std::nullopt})));

        EXPECT_CALL(batterySource, GetRaw())
            .WillOnce(Return(Error::Api::Result<std::vector<std::byte>>(
                std::vector<std::byte>{std::byte{0x03}})));
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
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources{{Api::ChannelId{.Value = "battery.main"}, &batterySource}};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseValidator, receiver, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = false})));

        EXPECT_CALL(receiver, TryReceive())
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000},
                    .Payload = {
                        std::byte{'l'}, std::byte{'e'}, std::byte{'a'},
                        std::byte{'s'}, std::byte{'e'}, std::byte{'-'}, std::byte{'1'}}})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                std::optional<::PiSubmarine::Udp::Api::Datagram>{std::nullopt})));

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
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources{{Api::ChannelId{.Value = "battery.main"}, &batterySource}};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseValidator, receiver, sender);

        EXPECT_CALL(receiver, TryReceive())
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000},
                    .Payload = {
                        std::byte{'l'}, std::byte{'e'}, std::byte{'a'},
                        std::byte{'s'}, std::byte{'e'}, std::byte{'-'}, std::byte{'1'}}})))
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

    TEST(ServerTest, TickStillSendsEmptyDatagramWhenNoChannelPayloadIsAvailable)
    {
        StrictMock<Api::IRawSourceMock> batterySource;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<::PiSubmarine::Udp::Api::IReceiverMock> receiver;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Server::Sources sources{{Api::ChannelId{.Value = "battery.main"}, &batterySource}};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(sources, resourceRegistry, leaseValidator, receiver, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})));

        EXPECT_CALL(receiver, TryReceive())
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                ::PiSubmarine::Udp::Api::Datagram{
                    .Peer = ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000},
                    .Payload = {
                        std::byte{'l'}, std::byte{'e'}, std::byte{'a'},
                        std::byte{'s'}, std::byte{'e'}, std::byte{'-'}, std::byte{'1'}}})))
            .WillOnce(Return(Error::Api::Result<std::optional<::PiSubmarine::Udp::Api::Datagram>>(
                std::optional<::PiSubmarine::Udp::Api::Datagram>{std::nullopt})));

        EXPECT_CALL(batterySource, GetRaw())
            .WillOnce(Return(std::unexpected(Error::Api::MakeError(Error::Api::ErrorCondition::CommunicationError))));
        EXPECT_CALL(sender, Send(testing::Truly([](const ::PiSubmarine::Udp::Api::Datagram& datagram)
            {
                return datagram.Payload == EncodeDatagram({});
            })))
            .WillOnce(Return(Error::Api::Result<void>{}));

        server.Tick(
            std::chrono::nanoseconds(std::chrono::milliseconds(100)),
            std::chrono::nanoseconds(std::chrono::milliseconds(10)));
    }
}
