using System.Buffers.Binary;
using System.Text;
using PicoUart.Hid;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class ProtocolTests
{
    internal static byte[] IdentityPayload()
    {
        var data = new byte[63];
        data[0] = 1;
        Encoding.ASCII.GetBytes("PicoUart").CopyTo(data, 1);
        Encoding.ASCII.GetBytes("0123456789ABCDEF").CopyTo(data, 33);
        data[49] = 1;
        data[50] = 2;
        data[51] = 3;
        data[52] = 2;
        data[53] = 6;
        data[54] = 3;
        data[55] = 60;
        data[56] = 7;
        data[58] = 31;
        Put32(data, 59, 532480);
        return data;
    }

    internal static BoardIdentity Identity() => Protocol.ParseIdentity(IdentityPayload());

    internal static byte[] ChannelPayload()
    {
        var data = new byte[54];
        data[0] = 1;
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(1), 65535);
        Put32(data, 3, 700);
        data[7] = 0xc1;
        data[8] = 255;
        Put32(data, 9, 150000);
        Put32(data, 13, 200000);
        Put32(data, 17, 0x80000000);
        Put32(data, 21, uint.MaxValue);
        Put32(data, 25, 115200);
        data[29] = 8;
        data[30] = 1;
        data[32] = 20;
        data[33] = 21;
        Put32(data, 34, 1);
        Put32(data, 38, 2);
        Put32(data, 42, 3);
        Put32(data, 46, uint.MaxValue);
        Put32(data, 50, uint.MaxValue);
        return data;
    }

    internal static byte[] ControlPayload(byte command = 2, byte flags = 0, byte status = 2, uint id = 1,
        uint requested = uint.MaxValue, byte error = 0)
    {
        var data = new byte[24];
        data[0] = 1;
        data[1] = command;
        data[2] = flags;
        data[3] = status;
        Put32(data, 4, id);
        Put32(data, 8, uint.MaxValue);
        Put32(data, 16, requested);
        data[21] = 1;
        data[22] = error;
        return data;
    }

    internal static void Put32(byte[] data, int offset, uint value) =>
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(offset), value);

    [Fact]
    public void StaticIdentityContainsFirmwareCapabilitiesAndUnknownMcu()
    {
        var data = IdentityPayload();
        data[52] = 255;
        var identity = Protocol.ParseIdentity(data);
        Assert.Equal("PicoUart", identity.ProductName);
        Assert.Equal("0123456789ABCDEF", identity.SerialNumber);
        Assert.Equal("1.2.3", identity.FirmwareVersion);
        Assert.Equal("Unknown MCU (255)", identity.Mcu);
        Assert.Equal(7, identity.Capabilities);
        Assert.Equal(31, identity.ClockProfileMask);
        Assert.Equal(532480u, identity.RamTotalBytes);
    }

    [Theory]
    [InlineData(0, 2)]
    [InlineData(1, 0)]
    [InlineData(10, 88)]
    [InlineData(33, 71)]
    [InlineData(53, 5)]
    [InlineData(54, 7)]
    [InlineData(55, 124)]
    [InlineData(56, 8)]
    [InlineData(58, 30)]
    [InlineData(58, 63)]
    public void IdentityRejectsMalformedFields(int offset, byte value)
    {
        var data = IdentityPayload();
        data[offset] = value;
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseIdentity(data));
    }

    [Fact]
    public void IdentityRejectsZeroRamAndLegacyLayout()
    {
        var data = IdentityPayload();
        Put32(data, 59, 0);
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseIdentity(data));
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseIdentity(data.AsSpan(0, 49)));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    public void TelemetryUsesStaticCapabilitiesAndTotalRam(byte flags)
    {
        var data = TelemetryPayload();
        data[1] = flags;
        var decoded = Protocol.ParseTelemetry(data, Identity());
        Assert.Equal(new LedStatus(flags != 0, flags != 0), decoded.Led);
        Assert.Equal(-1.25, decoded.TemperatureCelsius);
        Assert.Equal(4294967296ul, decoded.Runtime.UptimeMs);
        Assert.Equal(new ushort?[] { 10000, null }, decoded.Runtime.CpuUsageBasisPoints);
        Assert.Equal(65536u, decoded.Runtime.RamReservedBytes);
        Assert.Equal(532480u, decoded.Runtime.RamTotalBytes);
        Assert.Equal(280000000u, decoded.SystemClockHz);
        data[1] = 0;
        Assert.Equal(new LedStatus(null, null),
            Protocol.ParseTelemetry(data, Identity() with { Capabilities = 6 }).Led);
    }

    private static byte[] TelemetryPayload()
    {
        var data = new byte[24];
        data[0] = 1;
        BinaryPrimitives.WriteInt16LittleEndian(data.AsSpan(2), -125);
        BinaryPrimitives.WriteUInt64LittleEndian(data.AsSpan(4), 4294967296);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(12), 10000);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(14), ushort.MaxValue);
        Put32(data, 16, 65536);
        Put32(data, 20, 280000000);
        return data;
    }

    [Fact]
    public void TelemetryRejectsOldAndInconsistentValues()
    {
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseTelemetry(new byte[59], Identity()));
        foreach (var (offset, value) in new (int, byte)[] { (0, 2), (1, 2), (13, 254), (15, 254), (18, 255) })
        {
            var data = TelemetryPayload();
            data[offset] = value;
            Assert.Throws<InvalidOperationException>(() => Protocol.ParseTelemetry(data, Identity()));
        }
        var clock = TelemetryPayload();
        Put32(clock, 20, 0);
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseTelemetry(clock, Identity()));
    }

    [Theory]
    [InlineData(10, 0)]
    [InlineData(15, 5)]
    public void CombinedSnapshotUsesFullWidthDeltasAndSharedFaultTime(byte id, int index)
    {
        var decoded = Protocol.ParseChannel(id, ChannelPayload());
        Assert.Equal(index, decoded.Id);
        Assert.Equal(ushort.MaxValue, decoded.Sequence);
        Assert.Equal(700u, decoded.IntervalMs);
        Assert.Equal(4080, decoded.RingHighWatermark);
        Assert.Equal(150000u, decoded.ControllerTxBytes);
        Assert.Equal(200000u, decoded.ControllerRxBytes);
        Assert.Equal(0x80000000u, decoded.CdcTxBytes);
        Assert.Equal(uint.MaxValue, decoded.CdcRxBytes);
        Assert.Equal("N", decoded.Diagnostics.Parity);
        Assert.Equal((byte)20, decoded.Diagnostics.TxPin);
        Assert.Equal(new FaultHistory(uint.MaxValue), decoded.Diagnostics.Faults["rx_error"]);
        Assert.Equal(uint.MaxValue, decoded.Diagnostics.LastFaultUptimeS);
    }

    [Fact]
    public void AllZeroFaultCountsRequireZeroSharedTimestampButNonzeroCountsMayHaveZeroTime()
    {
        var data = ChannelPayload();
        Put32(data, 50, 0);
        Protocol.ParseChannel(10, data);
        data.AsSpan(34, 16).Clear();
        data[7] = 1;
        Protocol.ParseChannel(10, data);
        Put32(data, 50, 1);
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseChannel(10, data));
    }

    [Theory]
    [InlineData(0, 2)]
    [InlineData(7, 1)]
    [InlineData(29, 4)]
    [InlineData(30, 3)]
    [InlineData(31, 3)]
    [InlineData(32, 48)]
    [InlineData(33, 254)]
    public void ChannelRejectsMalformedSnapshot(int offset, byte value)
    {
        var data = ChannelPayload();
        data[offset] = value;
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseChannel(10, data));
    }

    [Fact]
    public void ChannelRequiresAssignedIdExactLengthAndZeroUnavailableFraming()
    {
        var data = ChannelPayload();
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseChannel(9, data));
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseChannel(16, data));
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseChannel(10, new byte[59]));
        Put32(data, 25, 0);
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseChannel(10, data));
        data.AsSpan(29, 3).Clear();
        Assert.Null(Protocol.ParseChannel(10, data).Diagnostics.Parity);
    }

    [Theory]
    [InlineData(127, 1, 4, 5)]
    [InlineData(127, 128, 3, 1)]
    [InlineData(2, 128, 3, 1)]
    [InlineData(2, 1, 4, 5)]
    public void ControlAcceptsOrderedRejectionEchoes(byte command, byte flags, byte status, byte error) =>
        Assert.Equal(flags, Protocol.ParseControl(ControlPayload(command, flags, status, error: error), Identity()).Flags);

    [Theory]
    [InlineData(127, 128, 4, 5)]
    [InlineData(127, 0, 2, 0)]
    [InlineData(2, 1, 2, 0)]
    [InlineData(2, 0, 2, 1)]
    [InlineData(2, 0, 3, 5)]
    [InlineData(2, 0, 4, 1)]
    [InlineData(5, 0, 5, 7)]
    [InlineData(5, 0, 6, 6)]
    public void ControlRejectsInvalidEchoesOrErrorPairs(byte command, byte flags, byte status, byte error) =>
        Assert.Throws<InvalidOperationException>(() =>
            Protocol.ParseControl(ControlPayload(command, flags, status, error: error), Identity()));

    [Fact]
    public void SaveProfilesAndBootMetadataAreValidated()
    {
        var data = ControlPayload(5, 1, requested: 4);
        var decoded = Protocol.ParseControl(data, Identity());
        Assert.Equal(4u, decoded.RequestedProfile);
        Assert.Equal(new ConfigurationStatus("defaults", "erased", null, 0), decoded.Configuration);
        Assert.Throws<InvalidOperationException>(() =>
            Protocol.ParseControl(data, Identity() with { ClockProfileMask = 15 }));
        data[20] = 1;
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseControl(data, Identity()));
        data[21] = 0;
        Put32(data, 8, 0);
        Assert.Equal("stored", Protocol.ParseControl(data, Identity()).Configuration.Source);
        data[20] = 2;
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseControl(data, Identity()));
    }

    [Fact]
    public void MissingAcknowledgementMayEchoValidatedProfile()
    {
        var decoded = Protocol.ParseControl(ControlPayload(5, 0, 3, requested: 1, error: 3), Identity());
        Assert.Equal(1u, decoded.RequestedProfile);
        Assert.Equal(3, decoded.ErrorReason);
    }

    [Fact]
    public void UnidentifiableAndIdleResultsHaveNoEchoedRequest()
    {
        Protocol.ParseControl(ControlPayload(0, 0, 0, 0), Identity());
        Protocol.ParseControl(ControlPayload(0, 0, 3, 0, error: 1), Identity());
        Assert.Throws<InvalidOperationException>(() =>
            Protocol.ParseControl(ControlPayload(2, 0, 3, 0, error: 1), Identity()));
        Assert.Throws<InvalidOperationException>(() =>
            Protocol.ParseControl(ControlPayload(0, 0, 0, 2), Identity()));
    }

    [Theory]
    [InlineData(1)]
    [InlineData(2)]
    public void FeatureFramingRequiresExactPayloadAndPlatformPrefix(int prefix)
    {
        var data = IdentityPayload();
        byte[] report = prefix == 1 ? [1, .. data] : [1, 1, .. data];
        Assert.Equal(data, PicoUartHid.FeaturePayload(report, 1, 63, prefix));
        Assert.Throws<InvalidOperationException>(() => PicoUartHid.FeaturePayload([.. report, 0], 1, 63, prefix));
        report[0] = 3;
        Assert.Throws<InvalidOperationException>(() => PicoUartHid.FeaturePayload(report, 1, 63, prefix));
    }
}
