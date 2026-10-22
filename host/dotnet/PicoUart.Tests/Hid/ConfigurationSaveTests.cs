using System.Buffers.Binary;
using PicoUart.Hid;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class ConfigurationSaveTests
{
    [Theory]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    [InlineData(4)]
    [InlineData(5)]
    public void CommandsUseExactNewLayout(byte command)
    {
        var payload = Protocol.BuildCommand(command, 0x12345678, ProtocolTests.Identity());
        Assert.Equal(24, payload.Length);
        Assert.Equal(1, payload[0]);
        Assert.Equal(command, payload[1]);
        Assert.Equal(0x12345678u, BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(4)));
        Assert.All(payload.Skip(8), value => Assert.Equal(0, value));
    }

    [Fact]
    public void SavesRequireAdvertisedCapabilityProfileAndExplicitAcknowledgement()
    {
        var identity = ProtocolTests.Identity();
        var payload = Protocol.BuildCommand(5, 1, identity, 4, true);
        Assert.Equal(1, payload[2]);
        Assert.Equal(4u, BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(16)));
        Assert.Throws<ArgumentException>(() => Protocol.BuildCommand(5, 1, identity, 4));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            Protocol.BuildCommand(5, 1, identity with { ClockProfileMask = 15 }, 4, true));
        Assert.Throws<InvalidOperationException>(() =>
            Protocol.BuildCommand(5, 1, identity with { Capabilities = 3 }));
        Assert.Throws<InvalidOperationException>(() =>
            Protocol.BuildCommand(3, 1, identity with { Capabilities = 5 }));
        Assert.Throws<InvalidOperationException>(() =>
            Protocol.BuildCommand(2, 1, identity with { Capabilities = 6 }));
        Assert.Throws<ArgumentException>(() => Protocol.BuildCommand(5, 0, identity));
    }

    [Fact]
    public void RequestIdsAdvanceAfterAllocationSkipLatestAndNeverWrap()
    {
        ulong next = 1;
        Assert.Equal(2u, PicoUartHid.AllocateRequestId(ref next, 1));
        Assert.Equal(3u, PicoUartHid.AllocateRequestId(ref next, 99));
        next = uint.MaxValue;
        Assert.Equal(uint.MaxValue, PicoUartHid.AllocateRequestId(ref next, 99));
        Assert.Throws<InvalidOperationException>(() => PicoUartHid.AllocateRequestId(ref next, 1));
        next = uint.MaxValue;
        Assert.Throws<InvalidOperationException>(() => PicoUartHid.AllocateRequestId(ref next, uint.MaxValue));
    }

    [Fact]
    public void PollsMatchingPendingResultThenReturnsVerifiedCompletion()
    {
        var responses = new Queue<ControlResult>([Result(status: 1), Result()]);
        Assert.Equal(2, PicoUartHid.WaitForCommand(5, 1, 7, 1, timeout =>
        {
            Assert.InRange(timeout, 1, 3000);
            return responses.Dequeue();
        }).Status);
        Assert.Empty(responses);
    }

    [Theory]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    [InlineData(4)]
    public void CorrelationRejectsOtherRequestCommandFlagsOrProfile(int field)
    {
        var result = field switch
        {
            1 => Result() with { RequestId = 8 },
            2 => Result() with { Command = 4 },
            3 => Result() with { Flags = 0 },
            _ => Result() with { RequestedProfile = 2 }
        };
        Assert.Throws<IOException>(() => PicoUartHid.WaitForCommand(5, 1, 7, 1, _ => result));
    }

    [Theory]
    [InlineData(3, 3)]
    [InlineData(4, 5)]
    [InlineData(5, 6)]
    [InlineData(6, 7)]
    [InlineData(6, 8)]
    [InlineData(6, 9)]
    public void RejectionsNeverCountAsSuccess(byte status, byte error)
    {
        Assert.Throws<InvalidOperationException>(() =>
            PicoUartHid.WaitForCommand(5, 1, 7, 1, _ => Result(status, error)));
    }

    [Fact]
    public void DeadlineBoundsPollingAndRejectsLateCompletion()
    {
        var reads = 0;
        Assert.Throws<TimeoutException>(() =>
            PicoUartHid.WaitForCommand(5, 1, 7, 1, _ => { reads++; return Result(); }, timeoutMs: 0));
        Assert.Equal(0, reads);
        Assert.Throws<TimeoutException>(() =>
            PicoUartHid.WaitForCommand(5, 1, 7, 1, _ =>
            {
                Thread.Sleep(30);
                return Result();
            }, timeoutMs: 10));
    }

    private static ControlResult Result(byte status = 2, byte error = 0) =>
        new(5, 1, status, 7, 1, new("defaults", "erased", null, 0), error);
}
