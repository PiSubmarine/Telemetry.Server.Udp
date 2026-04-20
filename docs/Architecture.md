# Telemetry.Server.Udp

`PiSubmarine.Telemetry.Server.Udp` maps telemetry leases to UDP endpoints and
sends the current telemetry snapshot to subscribed clients.

## Responsibility

The server owns:

- registration of the telemetry lease resource
- validation of telemetry subscriptions against `Lease.Api`
- mapping `LeaseId -> UDP endpoint`
- sending the current snapshot to all valid subscribers on tick

It does not own:

- telemetry snapshot production
- lease issuance
- socket polling implementation

## Subscription model

- clients acquire a telemetry lease elsewhere
- clients subscribe by providing `LeaseId` and their observed UDP endpoint
- resubscribing with the same `LeaseId` replaces the stored endpoint
- invalid or expired leases are removed during server ticks

## Packet format

The current implementation serializes snapshots into a simple textual payload so
the lease/subscription flow can be exercised before a protobuf schema exists.
This is intentionally temporary and should later move to a dedicated telemetry
protocol module.
