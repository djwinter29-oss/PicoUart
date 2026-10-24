using PicoUart.Hid;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class ConfigurationSaveTests
{
    [Fact]
    public void BuildActionUsesReport20Layout()
    {
        var identity = ProtocolTests.Identity();
        Assert.Equal(new byte[] { 1, 1, 0, 0 }, Protocol.BuildAction(1, identity));
        Assert.Equal(new byte[] { 1, 3, 1, 0 }, Protocol.BuildAction(3, identity));
        Assert.Equal(new byte[] { 1, 4, 3, 4 }, Protocol.BuildAction(4, identity, 4, true));
    }

    [Fact]
    public void SaveRequiresCapabilityProfileAndAcknowledgement()
    {
        var identity = ProtocolTests.Identity();
        Assert.Throws<ArgumentException>(() => Protocol.BuildAction(4, identity, 3, false));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            Protocol.BuildAction(4, identity with { ClockProfileMask = 0x0F }, 4, true));
        Assert.Throws<InvalidOperationException>(() =>
            Protocol.BuildAction(4, identity with { Capabilities = 3 }, 0, false));
    }

    [Fact]
    public void WaitForActionReturnsMatchingCompletion()
    {
        var responses = new Queue<ActionResult>([
            new(4, 1, 0),
            new(4, 2, 0)
        ]);
        var result = PicoUartHid.WaitForAction(4, _ => responses.Dequeue());
        Assert.Equal(2, result.Status);
        Assert.Empty(responses);
    }

    [Fact]
    public void WaitForActionRejectsMismatchedOrRejectedResults()
    {
        Assert.Throws<IOException>(() => PicoUartHid.WaitForAction(4, _ => new(3, 2, 0)));
        Assert.Throws<InvalidOperationException>(() => PicoUartHid.WaitForAction(4, _ => new(4, 5, 6)));
    }

    [Fact]
    public void WaitForActionTimesOutWithoutCompletion()
    {
        Assert.Throws<IOException>(() => PicoUartHid.WaitForAction(4, _ => new(4, 1, 0), timeoutMs: 1));
    }
}
