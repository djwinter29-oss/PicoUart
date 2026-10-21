using System.Buffers.Binary;
using PicoUart.Hid;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class ConfigurationSaveTests
{
    [Theory]
    [InlineData(0, 0u)]
    [InlineData(0, 1u)]
    [InlineData(0, uint.MaxValue)]
    [InlineData(1, 2u)]
    [InlineData(2, 0u)]
    [InlineData(2, 1u)]
    [InlineData(3, 2u)]
    [InlineData(3, uint.MaxValue)]
    [InlineData(4, 1u)]
    [InlineData(5, 2u)]
    public void DecodesConfigurationSaveStatusAndPlatformFraming(byte status, uint profile)
    {
        byte[] payload = [1, status, 0, 0, 0, 0, 0, 0];
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), profile);
        var expected = new ConfigurationSaveStatus(status, profile == uint.MaxValue ? null : profile);
        Assert.Equal(expected, Protocol.ParseConfigurationSave(payload));
        foreach (var prefix in new[] { 1, 2 })
        {
            byte[] report = prefix == 1 ? [17, .. payload] : [17, 17, .. payload];
            Assert.Equal(expected, Protocol.ParseConfigurationSave(PicoUartHid.FeaturePayload(report, 17, 8, prefix)));
        }
    }

    [Theory]
    [InlineData(0, 2)]
    [InlineData(1, 6)]
    [InlineData(2, 1)]
    [InlineData(3, 1)]
    [InlineData(4, 5)]
    public void RejectsMalformedConfigurationSaveReports(int offset, byte value)
    {
        byte[] payload = [1, 2, 0, 0, 0, 0, 0, 0];
        payload[offset] = value;
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseConfigurationSave(payload));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(7)]
    [InlineData(9)]
    public void RejectsWrongReportSizes(int size) =>
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseConfigurationSave(new byte[size]));

    [Theory]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(4)]
    [InlineData(5)]
    public void OnlyIdleOrInvalidRequestMayHaveUnavailableProfile(byte status) =>
        Assert.Throws<InvalidOperationException>(() =>
            Protocol.ParseConfigurationSave([1, status, 0, 0, 255, 255, 255, 255]));

    [Theory]
    [InlineData(0u, false, 1)]
    [InlineData(0u, false, 2)]
    [InlineData(0u, true, 1)]
    [InlineData(1u, true, 1)]
    [InlineData(2u, true, 2)]
    [InlineData(3u, true, 1)]
    [InlineData(4u, true, 1)]
    [InlineData(3u, true, 2)]
    [InlineData(4u, true, 2)]
    public void BuildsExactRequestPayload(uint profile, bool acknowledgement, byte mcuId)
    {
        var payload = Protocol.BuildConfigurationSave(profile, acknowledgement, mcuId);
        Assert.Equal(new byte[] { 1, acknowledgement ? (byte)1 : (byte)0, 0, 0, (byte)profile, 0, 0, 0 }, payload);
        Assert.Equal(17, Protocol.ConfigurationSaveReportId);
        Assert.Equal(8, Protocol.ConfigurationSaveSize);
    }

    [Theory]
    [InlineData(5u, true, 1)]
    [InlineData(uint.MaxValue, false, 0)]
    [InlineData(0u, false, 0)]
    [InlineData(0u, false, 255)]
    [InlineData(1u, false, 1)]
    [InlineData(2u, false, 2)]
    [InlineData(1u, true, 2)]
    [InlineData(2u, true, 1)]
    [InlineData(1u, true, 0)]
    [InlineData(2u, true, 255)]
    public void RejectsUnsafeOrWrongFamilyRequests(uint profile, bool acknowledgement, byte mcuId) =>
        Assert.ThrowsAny<ArgumentException>(() => Protocol.BuildConfigurationSave(profile, acknowledgement, mcuId));

    [Fact]
    public void PreflightRejectsPendingSaveWithoutWritingAnotherRequest()
    {
        var writes = 0;
        var error = Assert.Throws<InvalidOperationException>(() => PicoUartHid.SaveConfigurationRequest(
            Protocol.BuildConfigurationSave(1, true, 1), () => new(1, 1), _ => writes++));
        Assert.Contains("already pending", error.Message);
        Assert.Equal(0, writes);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(2)]
    [InlineData(3)]
    [InlineData(4)]
    [InlineData(5)]
    public void PreflightAllowsCompletedOrIdleRequestsAndSendsExactReport(byte status)
    {
        var responses = new Queue<ConfigurationSaveStatus>([new(status, status is 0 or 3 ? null : 1), new(2, 0)]);
        byte[]? written = null;
        Assert.Equal(new ConfigurationSaveStatus(2, 0), PicoUartHid.SaveConfigurationRequest(
            Protocol.BuildConfigurationSave(0, false, 1), responses.Dequeue, report => written = report));
        Assert.Equal(new byte[] { 17, 1, 0, 0, 0, 0, 0, 0, 0 }, written);
        Assert.Empty(responses);
    }

    [Fact]
    public void PollsUntilTheRequestedProfileIsSaved()
    {
        var responses = new Queue<ConfigurationSaveStatus>([new(1, 1), new(2, 1)]);
        Assert.Equal(new ConfigurationSaveStatus(2, 1),
            PicoUartHid.WaitForConfigurationSave(1, responses.Dequeue));
        Assert.Empty(responses);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(4)]
    [InlineData(5)]
    public void RejectsResultForAnotherProfile(byte status) =>
        Assert.Throws<InvalidOperationException>(() =>
            PicoUartHid.WaitForConfigurationSave(1, () => new(status, 2)));

    [Fact]
    public void RejectsUnavailableIdleResultAfterWriting() =>
        Assert.Throws<InvalidOperationException>(() =>
            PicoUartHid.WaitForConfigurationSave(1, () => new(0, null)));

    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    public void PollingIsBoundedWhileFirmwareHasNotCompleted(byte status) =>
        Assert.Throws<TimeoutException>(() =>
            PicoUartHid.WaitForConfigurationSave(0, () => new(status, 0), timeoutMs: 0));

    [Theory]
    [InlineData(3, "rejected")]
    [InlineData(4, "CDC")]
    public void SurfacesFirmwareRejectionAndBusyErrors(byte status, string message)
    {
        var error = Assert.Throws<InvalidOperationException>(() =>
            PicoUartHid.WaitForConfigurationSave(1, () => new(status, status == 3 ? null : 1)));
        Assert.Contains(message, error.Message);
    }

    [Fact]
    public void SurfacesFlashFailures() =>
        Assert.Throws<IOException>(() => PicoUartHid.WaitForConfigurationSave(1, () => new(5, 1)));

}
