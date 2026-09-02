# KnobKraft local session protocol v1

The session transport uses a four-byte, unsigned, big-endian payload length followed by one UTF-8 JSON document. Frames are limited to 4 MiB by default. A receiver validates the length before reserving payload storage. Empty, oversized, malformed JSON, and non-object messages are rejected.

The server binds an ephemeral port on `127.0.0.1` only. For each run it atomically publishes a discovery JSON file containing `protocolMajor`, `protocolMinor`, `port`, `processId`, `generationId`, `authenticationToken`, and `writtenAtUnixMillis`. The directory, clock, and random-token source are injectable. Production callers choose the per-user application-data path; tests always use a temporary directory. The server refreshes the timestamp while running and only removes a discovery record that still belongs to its generation.

Every request envelope has this shape:

```json
{
  "kind": "request",
  "protocolMajor": 1,
  "protocolMinor": 0,
  "token": "per-run secret",
  "requestId": "globally unique mutation or query ID",
  "operation": "getServerInfo",
  "context": {
    "requestId": "same ID",
    "clientId": "connection-stable client ID",
    "pluginInstanceId": "DAW-project-stable instance ID",
    "deadlineUnixMillis": 1700000005000
  },
  "body": {}
}
```

The authentication token is required on every request and is never included in responses, events, or normal logs. Protocol v1 accepts any minor version in a major-version-1 envelope; an unknown major version returns `ProtocolIncompatible`. A successful response carries the same `requestId`, `ok: true`, and a `body`. A failed response carries `ok: false` and the stable `ServiceError` code, human-readable message, and retryable flag.

Supported operations and request bodies:

| Operation | Body fields |
| --- | --- |
| `getServerInfo` | none |
| `listConfiguredSynthInstances` | `pageSize`, optional `pageToken` |
| `getConfiguredSynthInstance` | `configuredSynthInstanceId` |
| `searchPatches` | `query`, optional `adaptationId`, `pageSize`, optional `pageToken` |
| `getPatch` | `patchId` |
| `applyToEditBuffer` | `configuredSynthInstanceId`, `expectedAdaptationId`, complete `patch` |
| `getTransferStatus` | `transferId` |
| `cancelTransfer` | `transferId` |
| `openKnobKraft` | numeric `target`, optional `targetId` |
| `publishSession` | `instanceName`, optional `hostName`, `binding`, optional stored patch name/fingerprint |
| `disconnectSession` | none |

Session snapshots are unsolicited `event` envelopes. They contain the full observable snapshot, allowing a reconnecting client to replace rather than reconstruct state. Protocol-level heartbeats carry no project or patch data. The server closes stale connections; when a connection that published a session expires, it removes that client session through the serialized service executor.

The asynchronous client keeps unanswered requests across a connection loss. On a new server generation, it resends each request with its original ID and deadline. Mutating-request idempotency belongs to `SessionService`; this guarantees that an ambiguous network failure cannot enqueue a second hardware operation. Socket callbacks and observer callbacks never depend on JUCE, UI components, the database, Python, or MIDI objects.
