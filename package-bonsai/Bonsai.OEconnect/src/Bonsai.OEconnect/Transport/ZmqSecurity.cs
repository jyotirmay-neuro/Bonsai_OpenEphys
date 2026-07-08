using System;
using NetMQ;

namespace Bonsai.OEconnect.Transport;

/// <summary>
/// Client half of the CURVE handshake (spec §5.7).
///
/// The OE plugin refuses to bind a non-loopback address unless it holds a CURVE
/// server secret key. This is the matching client policy: to reach such a plugin
/// Bonsai must know the server's PUBLIC key and present a keypair of its own.
///
/// Configured entirely through environment variables on the Bonsai machine, so no
/// key material ends up in a saved workflow:
///
///   OEC_ZMQ_CURVE_SERVER_PUBLIC  the plugin's 40-char Z85 public key (required
///                                for any non-loopback endpoint)
///   OEC_ZMQ_CURVE_SECRET         this client's 40-char Z85 secret key (optional;
///                                an ephemeral keypair is generated when unset)
///
/// A loopback endpoint needs neither, matching the plugin's exemption.
/// </summary>
internal static class ZmqSecurity
{
    private const string ServerPublicVar = "OEC_ZMQ_CURVE_SERVER_PUBLIC";
    private const string ClientSecretVar = "OEC_ZMQ_CURVE_SECRET";
    private const int Z85KeyLength = 40;

    /// <summary>
    /// True when every endpoint stays on this host. Mirrors the plugin's
    /// is_loopback_endpoint(): tcp to 127.x / ::1 / localhost, or the kernel-local
    /// ipc:// and inproc:// schemes.
    /// </summary>
    public static bool IsLoopback(string endpoint)
    {
        if (string.IsNullOrEmpty(endpoint)) return false;
        if (endpoint.StartsWith("ipc://", StringComparison.Ordinal) ||
            endpoint.StartsWith("inproc://", StringComparison.Ordinal)) return true;
        return endpoint.Contains("127.0.0.1")
            || endpoint.Contains("[::1]")
            || endpoint.Contains("::1")
            || endpoint.Contains("localhost");
    }

    /// <summary>
    /// Resolves the CURVE configuration for a connection, or null when the
    /// endpoints are loopback-only and plaintext is permitted.
    /// Throws when a non-loopback endpoint has no server public key: failing
    /// closed rather than silently attempting an unauthenticated connection that
    /// the plugin would refuse anyway.
    /// </summary>
    public static CurveConfig? Resolve(string dataEndpoint, string cmdEndpoint)
    {
        var serverPublic = Environment.GetEnvironmentVariable(ServerPublicVar);
        var loopback = IsLoopback(dataEndpoint) && IsLoopback(cmdEndpoint);

        if (string.IsNullOrEmpty(serverPublic))
        {
            if (loopback) return null;   // plaintext localhost, same as the plugin
            throw new InvalidOperationException(
                $"Endpoint '{dataEndpoint}|{cmdEndpoint}' is not loopback, so the OEconnect " +
                $"plugin will only accept a CURVE-authenticated connection. Set the " +
                $"{ServerPublicVar} environment variable to the plugin's 40-character Z85 " +
                $"public key before starting Bonsai.");
        }

        if (serverPublic!.Length != Z85KeyLength)
            throw new InvalidOperationException(
                $"{ServerPublicVar} must be a {Z85KeyLength}-character Z85 key; got {serverPublic.Length}.");

        var clientSecret = Environment.GetEnvironmentVariable(ClientSecretVar);
        NetMQCertificate clientCert;
        if (string.IsNullOrEmpty(clientSecret))
        {
            /* No client identity configured. CURVE still authenticates the *server*
             * and encrypts the channel; an ephemeral client keypair is enough unless
             * the plugin also whitelists client keys. */
            clientCert = new NetMQCertificate();
        }
        else
        {
            if (clientSecret!.Length != Z85KeyLength)
                throw new InvalidOperationException(
                    $"{ClientSecretVar} must be a {Z85KeyLength}-character Z85 key; got {clientSecret.Length}.");
            clientCert = NetMQCertificate.CreateFromSecretKey(clientSecret);
        }

        var serverCert = NetMQCertificate.FromPublicKey(serverPublic);
        return new CurveConfig(clientCert, serverCert.PublicKey);
    }

    /// <summary>Applies CURVE to a client socket. No-op when <paramref name="cfg"/> is null.</summary>
    public static void Apply(NetMQSocket socket, CurveConfig? cfg)
    {
        if (cfg is null) return;
        socket.Options.CurveServer = false;          // this side is the client
        socket.Options.CurveCertificate = cfg.ClientCertificate;
        socket.Options.CurveServerKey = cfg.ServerPublicKey;
    }
}

internal sealed record CurveConfig(NetMQCertificate ClientCertificate, byte[] ServerPublicKey);
