#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "PiSubmarine/Error/Api/MakeError.h"
#include "PiSubmarine/Lease/Api/ILeaseValidatorMock.h"
#include "PiSubmarine/Lease/Api/IResourceRegistryMock.h"
#include "PiSubmarine/Telemetry/Api/ISourceMock.h"
#include "PiSubmarine/Telemetry/Server/Udp/ErrorCode.h"
#include "PiSubmarine/Telemetry/Server/Udp/Server.h"
#include "PiSubmarine/Udp/Api/ISenderMock.h"

namespace PiSubmarine::Telemetry::Server::Udp
{
    namespace
    {
        using ::testing::_;
        using ::testing::Return;
        using ::testing::StrictMock;
    }

    TEST(ServerTest, RegistersTelemetryResourceOnConstruction)
    {
        StrictMock<Api::ISourceMock> source;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;

        EXPECT_CALL(resourceRegistry, RegisterResource(Lease::Api::ResourceDescriptor{
                        .Id = Lease::Api::ResourceId{.Value = "telemetry-main"},
                        .Policy = Lease::Api::LeasePolicy{
                            .MaxLeases = std::nullopt,
                            .LeaseDuration = std::chrono::milliseconds(3000)}}))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(source, resourceRegistry, leaseValidator, sender);
    }

    TEST(ServerTest, SubscribeReturnsErrorWhenLeaseIsInvalid)
    {
        StrictMock<Api::ISourceMock> source;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(source, resourceRegistry, leaseValidator, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = false})));

        const auto result = server.Subscribe(
            Lease::Api::LeaseId{.Value = "lease-1"},
            ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000});

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(
            result.error(),
            Error::Api::MakeError(
                Error::Api::ErrorCondition::ContractError,
                make_error_code(ErrorCode::InvalidTelemetryLease)));
    }

    TEST(ServerTest, TickSendsSnapshotToValidSubscribers)
    {
        StrictMock<Api::ISourceMock> source;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Api::Snapshot snapshot{};
        snapshot.Depth = 2.0_m;

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(source, resourceRegistry, leaseValidator, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})));

        ASSERT_TRUE(server.Subscribe(
            Lease::Api::LeaseId{.Value = "lease-1"},
            ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000}).has_value());

        EXPECT_CALL(source, GetSnapshot())
            .WillOnce(Return(snapshot));
        EXPECT_CALL(sender, Send(testing::Truly([](const ::PiSubmarine::Udp::Api::Datagram& datagram)
            {
                return datagram.Peer == ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000}
                    && !datagram.Payload.empty();
            })))
            .WillOnce(Return(Error::Api::Result<void>{}));

        server.Tick(
            std::chrono::nanoseconds(std::chrono::milliseconds(100)),
            std::chrono::nanoseconds(std::chrono::milliseconds(10)));
    }

    TEST(ServerTest, SubscribeReplacesStoredEndpointForSameLease)
    {
        StrictMock<Api::ISourceMock> source;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Api::Snapshot snapshot{};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(source, resourceRegistry, leaseValidator, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})));

        ASSERT_TRUE(server.Subscribe(
            Lease::Api::LeaseId{.Value = "lease-1"},
            ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000}).has_value());
        ASSERT_TRUE(server.Subscribe(
            Lease::Api::LeaseId{.Value = "lease-1"},
            ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9001}).has_value());

        EXPECT_CALL(source, GetSnapshot())
            .WillOnce(Return(snapshot));
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
        StrictMock<Api::ISourceMock> source;
        StrictMock<Lease::Api::IResourceRegistryMock> resourceRegistry;
        StrictMock<Lease::Api::ILeaseValidatorMock> leaseValidator;
        StrictMock<::PiSubmarine::Udp::Api::ISenderMock> sender;
        Api::Snapshot snapshot{};

        EXPECT_CALL(resourceRegistry, RegisterResource(_))
            .WillOnce(Return(Error::Api::Result<void>{}));

        Server server(source, resourceRegistry, leaseValidator, sender);

        EXPECT_CALL(leaseValidator, ValidateLease(
                        Lease::Api::LeaseId{.Value = "lease-1"},
                        Lease::Api::ResourceId{.Value = "telemetry-main"}))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = true})))
            .WillOnce(Return(Error::Api::Result<Lease::Api::LeaseValidation>(
                Lease::Api::LeaseValidation{.IsValid = false})));

        ASSERT_TRUE(server.Subscribe(
            Lease::Api::LeaseId{.Value = "lease-1"},
            ::PiSubmarine::Udp::Api::Endpoint{"127.0.0.1", 9000}).has_value());

        EXPECT_CALL(source, GetSnapshot())
            .WillOnce(Return(snapshot));

        server.Tick(
            std::chrono::nanoseconds(std::chrono::milliseconds(100)),
            std::chrono::nanoseconds(std::chrono::milliseconds(10)));
    }
}
