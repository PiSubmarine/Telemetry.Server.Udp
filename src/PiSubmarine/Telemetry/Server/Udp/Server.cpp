#include "PiSubmarine/Telemetry/Server/Udp/Server.h"

#include <sstream>
#include <string>
#include <vector>

namespace PiSubmarine::Telemetry::Server::Udp
{
	Server::Server(
		Api::ISource& source,
		Lease::Api::IResourceRegistry& resourceRegistry,
		const Lease::Api::ILeaseValidator& leaseValidator,
		::PiSubmarine::Udp::Api::IReceiver& receiver,
		::PiSubmarine::Udp::Api::ISender& sender
	)
		: m_Source(source),
		  m_ResourceRegistry(resourceRegistry),
		  m_LeaseValidator(leaseValidator),
		  m_Receiver(receiver),
		  m_Sender(sender)
	{
		const Lease::Api::ResourceDescriptor resourceDescriptor{
			.Id = MakeTelemetryResourceId(),
			.Policy = Lease::Api::LeasePolicy{
				.MaxLeases = std::nullopt,
				.LeaseDuration = std::chrono::milliseconds(3000)
			}
		};

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
			if (!receiveResult.has_value())
			{
				break;
			}

			if (!receiveResult->has_value())
			{
				break;
			}

			HandleSubscriptionDatagram(receiveResult->value(), m_LeaseValidator, m_Subscribers);
		}

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
				.Payload = payload
			};

			const auto sendResult = m_Sender.Send(datagram);
			static_cast<void>(sendResult);
			++iterator;
		}
	}

	Lease::Api::ResourceId Server::MakeTelemetryResourceId()
	{
		return Lease::Api::ResourceId{.Value = "telemetry-main"};
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
			.Endpoint = datagram.Peer
		};
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
