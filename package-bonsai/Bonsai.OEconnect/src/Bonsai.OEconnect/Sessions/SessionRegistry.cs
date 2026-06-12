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

    private static string ResolveEndpoint(string endpoint)
    {
        if (!string.IsNullOrEmpty(endpoint)) return endpoint;
        /* Auto-discovery: scan sidecar dir for the freshest live JSON. */
        var dir = SidecarDiscovery.Dir();
        var freshest = SidecarDiscovery.FindNewestLive(dir, maxAgeSeconds: 5);
        if (freshest is null)
            throw new Data.OpenEphysConnectionException("Auto", "", 0,
                "No live OEconnect session found via sidecar discovery.");
        /* Prefer shmem on same OS. */
        return string.IsNullOrEmpty(freshest.Value.Shm)
            ? freshest.Value.Zmq
            : freshest.Value.Shm;
    }
}
