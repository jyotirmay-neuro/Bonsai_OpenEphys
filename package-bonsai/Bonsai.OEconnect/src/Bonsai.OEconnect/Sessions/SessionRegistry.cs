using System;
using System.Collections.Generic;
using Bonsai.OEconnect.Transport;

namespace Bonsai.OEconnect.Sessions;

internal static class SessionRegistry
{
    private static readonly object _lock = new();
    private static readonly Dictionary<string, Session> _sessions = new();

    public static Session Acquire(string endpoint)
    {
        lock (_lock)
        {
            if (_sessions.TryGetValue(endpoint, out var existing))
            {
                existing.AddRef();
                return existing;
            }
            var resolved = ResolveEndpoint(endpoint);
            ITransportClient client = resolved.StartsWith("tcp://", StringComparison.Ordinal)
                ? new ZmqClient()
                : (ITransportClient)new ShmemClient();
            if (!client.Start(resolved))
                throw new Data.OpenEphysConnectionException(client.Name, resolved, 0,
                    $"Failed to start transport for endpoint '{resolved}'.");

            /* Liveness gate for shared memory: a crashed producer can leave a
             * mappable-but-dead region behind. The producer refreshes
             * producer_heartbeat_ns ~1 Hz; reject anything staler than 5 s. */
            if (client is ShmemClient && !IsProducerAlive(client))
            {
                client.Stop();
                throw new Data.OpenEphysConnectionException(client.Name, resolved, 0,
                    $"Shared-memory region '{resolved}' has no live producer (stale heartbeat).");
            }

            var session = new Session(client, resolved);
            session.StartReader();
            session.AddRef();
            _sessions[endpoint] = session;
            return session;
        }
    }

    public static void Release(string endpoint, Session session)
    {
        lock (_lock)
        {
            if (session.Release() == 0 &&
                _sessions.TryGetValue(endpoint, out var s) &&
                ReferenceEquals(s, session))
            {
                _sessions.Remove(endpoint);
                session.Dispose();
            }
        }
    }

    private const int HeartbeatMaxAgeSeconds = 5;

    private static bool IsProducerAlive(ITransportClient client)
    {
        ulong hb = client.ProducerHeartbeatNs;
        /* Zero means the producer never stamped a heartbeat — a legacy producer
         * or one that hasn't published yet. We can't prove it's dead, so allow
         * it. Only a NON-zero but stale value is positive evidence of a crash. */
        if (hb == 0) return true;
        ulong nowNs = (ulong)DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() * 1_000_000UL;
        ulong ageNs = nowNs > hb ? nowNs - hb : hb - nowNs;  /* skew-tolerant */
        return ageNs <= (ulong)HeartbeatMaxAgeSeconds * 1_000_000_000UL;
    }

    private static string ResolveEndpoint(string endpoint)
    {
        if (!string.IsNullOrEmpty(endpoint)) return endpoint;
        /* Auto-discovery: scan sidecar dir for the newest session pointer.
         * Liveness is verified post-connect via the shm heartbeat, not here. */
        var dir = SidecarDiscovery.Dir();
        var freshest = SidecarDiscovery.FindNewest(dir);
        if (freshest is null)
            throw new Data.OpenEphysConnectionException("Auto", "", 0,
                "No live OEconnect session found via sidecar discovery.");
        /* Prefer shmem on same OS. */
        return string.IsNullOrEmpty(freshest.Value.Shm)
            ? freshest.Value.Zmq
            : freshest.Value.Shm;
    }
}
