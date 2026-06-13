# Telemetry.Server.Udp

`PiSubmarine.Telemetry.Server.Udp` maps telemetry leases to UDP endpoints and
sends the current batched raw telemetry payload to subscribed clients.

## Responsibility

The server owns:

- registration of the telemetry lease resource
- reception of UDP subscription packets
- validation of telemetry subscriptions against `Lease.Api`
- mapping `LeaseId -> UDP endpoint`
- collecting raw payloads from configured telemetry channels
- batching channel payloads into one UDP datagram
- sending the current batch to all valid subscribers on tick

It does not own:

- domain-specific telemetry serialization
- lease issuance
- socket polling implementation

## Subscription model

- clients acquire a telemetry lease elsewhere
- clients subscribe by sending a UDP datagram whose payload is the raw
  `LeaseId`
- the server uses the datagram source endpoint as the subscriber endpoint
- resubscribing with the same `LeaseId` replaces the stored endpoint
- invalid or expired leases are removed during server ticks

## Packet format

Each UDP datagram contains:

- a 32-bit big-endian channel count
- for each channel:
  the channel name length as 32-bit big-endian integer,
  the channel name bytes,
  the raw payload length as 32-bit big-endian integer,
  and the raw payload bytes

Channels whose `IRawSource::GetRaw()` fails are omitted from the datagram.
