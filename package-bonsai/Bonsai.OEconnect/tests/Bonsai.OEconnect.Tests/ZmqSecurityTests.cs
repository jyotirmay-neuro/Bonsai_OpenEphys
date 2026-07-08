using System;
using Bonsai.OEconnect.Transport;
using Xunit;

namespace Bonsai.OEconnect.Tests;

/// <summary>
/// Client half of spec §5.7. The plugin refuses an unauthenticated non-loopback
/// bind; this asserts Bonsai applies the mirror-image policy instead of silently
/// attempting a connection that could never complete.
/// </summary>
[Collection("EnvVars")]
public class ZmqSecurityTests : IDisposable
{
    private const string ServerPublicVar = "OEC_ZMQ_CURVE_SERVER_PUBLIC";
    private const string ClientSecretVar = "OEC_ZMQ_CURVE_SECRET";

    /* Valid 40-char Z85 keys (the canonical CurveZMQ test vectors). */
    private const string ServerPublic = "rq:rM>}U?@Lns47E1%kR.o@n%FcmmsL/@{H8]yf7";
    private const string ClientSecret = "D:)Q[IlAW!ahhC2ac:9*A}h:p?([4%wOTJ%JR%cs";

    public ZmqSecurityTests() => Clear();
    public void Dispose() => Clear();

    private static void Clear()
    {
        Environment.SetEnvironmentVariable(ServerPublicVar, null);
        Environment.SetEnvironmentVariable(ClientSecretVar, null);
    }

    [Theory]
    [InlineData("tcp://127.0.0.1:5557")]
    [InlineData("tcp://localhost:5557")]
    [InlineData("ipc:///tmp/oeconnect.data")]
    [InlineData("inproc://oeconnect")]
    public void LoopbackEndpointsAreRecognised(string endpoint)
        => Assert.True(ZmqSecurity.IsLoopback(endpoint));

    [Theory]
    [InlineData("tcp://192.168.1.10:5557")]
    [InlineData("tcp://0.0.0.0:5557")]
    [InlineData("tcp://example.org:5557")]
    public void RoutableEndpointsAreNotLoopback(string endpoint)
        => Assert.False(ZmqSecurity.IsLoopback(endpoint));

    [Fact]
    public void LoopbackWithoutKeysUsesPlaintext()
    {
        var cfg = ZmqSecurity.Resolve("tcp://127.0.0.1:5557", "tcp://127.0.0.1:5558");
        Assert.Null(cfg);   // plaintext permitted, matching the plugin's exemption
    }

    [Fact]
    public void NonLoopbackWithoutServerKeyFailsClosed()
    {
        var ex = Assert.Throws<InvalidOperationException>(
            () => ZmqSecurity.Resolve("tcp://192.168.1.10:5557", "tcp://192.168.1.10:5558"));
        Assert.Contains(ServerPublicVar, ex.Message);
    }

    [Fact]
    public void NonLoopbackWithServerKeyGeneratesEphemeralClientIdentity()
    {
        Environment.SetEnvironmentVariable(ServerPublicVar, ServerPublic);
        var cfg = ZmqSecurity.Resolve("tcp://192.168.1.10:5557", "tcp://192.168.1.10:5558");

        Assert.NotNull(cfg);
        Assert.Equal(32, cfg!.ServerPublicKey.Length);
        Assert.NotNull(cfg.ClientCertificate);
    }

    [Fact]
    public void ConfiguredClientSecretIsUsed()
    {
        Environment.SetEnvironmentVariable(ServerPublicVar, ServerPublic);
        Environment.SetEnvironmentVariable(ClientSecretVar, ClientSecret);

        var cfg = ZmqSecurity.Resolve("tcp://10.0.0.5:5557", "tcp://10.0.0.5:5558");
        Assert.NotNull(cfg);
        Assert.Equal(ClientSecret, cfg!.ClientCertificate.SecretKeyZ85);
    }

    [Fact]
    public void MalformedServerKeyIsRejected()
    {
        Environment.SetEnvironmentVariable(ServerPublicVar, "too-short");
        var ex = Assert.Throws<InvalidOperationException>(
            () => ZmqSecurity.Resolve("tcp://10.0.0.5:5557", "tcp://10.0.0.5:5558"));
        Assert.Contains("Z85", ex.Message);
    }

    [Fact]
    public void MalformedClientSecretIsRejected()
    {
        Environment.SetEnvironmentVariable(ServerPublicVar, ServerPublic);
        Environment.SetEnvironmentVariable(ClientSecretVar, "nope");
        var ex = Assert.Throws<InvalidOperationException>(
            () => ZmqSecurity.Resolve("tcp://10.0.0.5:5557", "tcp://10.0.0.5:5558"));
        Assert.Contains(ClientSecretVar, ex.Message);
    }

    /// <summary>Mixed endpoints must not be treated as loopback.</summary>
    [Fact]
    public void OneRoutableEndpointRequiresCurve()
    {
        var ex = Assert.Throws<InvalidOperationException>(
            () => ZmqSecurity.Resolve("tcp://127.0.0.1:5557", "tcp://192.168.1.10:5558"));
        Assert.Contains(ServerPublicVar, ex.Message);
    }
}
