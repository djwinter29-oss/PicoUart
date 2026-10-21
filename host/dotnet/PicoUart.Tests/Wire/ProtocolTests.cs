using System.Buffers.Binary;
using PicoUart.Hid;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class ProtocolTests
{
    [Fact]
    public void ChannelDiagnosticsDecodeAppliedSettingsAndLongUptime()
    {
        var data = new byte[59];
        data[0] = 1;
        data[1] = 0x81;
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(2), 115200);
        data[6] = 8;
        data[7] = 1;
        data[9] = 20;
        data[10] = 21;
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(47), 3);
        BinaryPrimitives.WriteUInt64LittleEndian(data.AsSpan(51), 4294967296ul);
        var decoded = Protocol.ParseChannelDiagnostics(data);
        Assert.Equal(115200u, decoded.BaudRate);
        Assert.Equal("N", decoded.Parity);
        Assert.Equal((byte)20, decoded.TxPin);
        Assert.Equal(new FaultHistory(3, 4294967296ul), decoded.Faults["rx_error"]);
        Assert.Equal(new FaultHistory(0, null), decoded.Faults["control_error"]);
        foreach (var (offset, value) in new (int, byte)[] { (0, 2), (6, 4), (7, 3), (8, 3), (9, 48), (10, 254) })
        {
            var invalid = (byte[])data.Clone();
            invalid[offset] = value;
            Assert.Throws<InvalidOperationException>(() => Protocol.ParseChannelDiagnostics(invalid));
        }
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseChannelDiagnostics(data.AsSpan(1)));
    }

    [Theory]
    [InlineData(0, null, null)]
    [InlineData(1, false, false)]
    [InlineData(5, false, true)]
    [InlineData(7, true, true)]
    public void LedReportsActualOutputAndManualState(byte flags, bool? manual, bool? output)
    {
        Assert.Equal(new LedStatus(manual, output), Protocol.ParseLed([1, flags]));
    }

    [Theory]
    [InlineData(2, 1)]
    [InlineData(1, 8)]
    [InlineData(1, 2)]
    [InlineData(1, 3)]
    [InlineData(1, 4)]
    public void LedRejectsInvalidState(byte version, byte flags)
    {
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseLed([version, flags]));
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseLed([]));
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseLed([1, 1, 0]));
    }

    [Fact]
    public void ConfigurationDecodesAndRejectsInvalidReportValues()
    {
        byte[] payload = new byte[12];
        payload[0] = 1;
        payload[2] = 3;
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), uint.MaxValue);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(8), 1);
        Assert.Equal(new ConfigurationStatus("defaults", "crc_failed", null, 1),
            Protocol.ParseConfiguration(payload));

        payload[1] = 1;
        payload[2] = 0;
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), 2);
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseConfiguration(payload));
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(8), 2);
        Assert.Equal(new ConfigurationStatus("stored", "none", 2, 2), Protocol.ParseConfiguration(payload));
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), 0);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(8), 1);
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseConfiguration(payload));
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(8), 0);
        Assert.Equal(new ConfigurationStatus("stored", "none", 0, 0), Protocol.ParseConfiguration(payload));

        payload[1] = 2;
        payload[2] = 6;
        Assert.Equal("recovery", Protocol.ParseConfiguration(payload).Source);

        foreach (var (offset, value) in new (int, byte)[] { (0, 2), (1, 3), (2, 8), (3, 1), (8, 5) })
        {
            var invalid = (byte[])payload.Clone();
            invalid[offset] = value;
            Assert.Throws<InvalidOperationException>(() => Protocol.ParseConfiguration(invalid));
        }
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseConfiguration(payload.AsSpan(1)));

        payload[1] = 1;
        payload[2] = 1;
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseConfiguration(payload));
        payload[1] = 2;
        payload[2] = 0;
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseConfiguration(payload));
    }

    [Fact]
    public void BoardIdentityAndRuntimeDecodeDeviceValues()
    {
        var identity = new byte[49];
        identity[0] = 1;
        System.Text.Encoding.ASCII.GetBytes("PicoUart").CopyTo(identity, 1);
        System.Text.Encoding.ASCII.GetBytes("0123456789ABCDEF").CopyTo(identity, 33);
        Assert.Equal(new BoardIdentity("PicoUart", "0123456789ABCDEF"), Protocol.ParseIdentity(identity));

        var runtime = new byte[21];
        runtime[0] = 1;
        BinaryPrimitives.WriteUInt64LittleEndian(runtime.AsSpan(1), 4294967296);
        BinaryPrimitives.WriteUInt16LittleEndian(runtime.AsSpan(9), 10000);
        BinaryPrimitives.WriteUInt16LittleEndian(runtime.AsSpan(11), ushort.MaxValue);
        BinaryPrimitives.WriteUInt32LittleEndian(runtime.AsSpan(13), 65536);
        BinaryPrimitives.WriteUInt32LittleEndian(runtime.AsSpan(17), 270336);
        var status = Protocol.ParseRuntime(runtime);
        Assert.Equal(4294967296ul, status.UptimeMs);
        Assert.Equal(new ushort?[] { 10000, null }, status.CpuUsageBasisPoints);
        Assert.Equal(65536u, status.RamReservedBytes);
        Assert.Equal(270336u, status.RamTotalBytes);

        identity[33] = (byte)'G';
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseIdentity(identity));
        identity[33] = (byte)'0';
        identity[10] = (byte)'X';
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseIdentity(identity));
        BinaryPrimitives.WriteUInt16LittleEndian(runtime.AsSpan(9), 10001);
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseRuntime(runtime));
        BinaryPrimitives.WriteUInt16LittleEndian(runtime.AsSpan(9), 0);
        BinaryPrimitives.WriteUInt32LittleEndian(runtime.AsSpan(17), 65535);
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseRuntime(runtime));
    }

    [Theory]
    [InlineData(3u, (byte)1)]
    [InlineData(4u, (byte)1)]
    [InlineData(3u, (byte)2)]
    [InlineData(4u, (byte)2)]
    public void AdditionalClockProfilesRoundTrip(uint profile, byte mcuId)
    {
        var configuration = new byte[12];
        configuration[0] = 1;
        configuration[1] = 1;
        BinaryPrimitives.WriteUInt32LittleEndian(configuration.AsSpan(4), profile);
        BinaryPrimitives.WriteUInt32LittleEndian(configuration.AsSpan(8), profile);
        Assert.Equal(new ConfigurationStatus("stored", "none", profile, profile),
            Protocol.ParseConfiguration(configuration));

        var request = Protocol.BuildConfigurationSave(profile, true, mcuId);
        Assert.Equal(profile, BinaryPrimitives.ReadUInt32LittleEndian(request.AsSpan(4)));
        request[1] = 2;
        Assert.Equal(new ConfigurationSaveStatus(2, profile), Protocol.ParseConfigurationSave(request));
        Assert.Throws<ArgumentException>(() => Protocol.BuildConfigurationSave(profile, false, mcuId));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(20)]
    [InlineData(22)]
    [InlineData(48)]
    [InlineData(50)]
    public void BoardTelemetryRejectsWrongSizes(int length)
    {
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseIdentity(new byte[length]));
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseRuntime(new byte[length]));
    }

    [Fact]
    public void BoardTelemetryRejectsUnknownVersions()
    {
        var identity = new byte[49];
        identity[0] = 2;
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseIdentity(identity));
        var runtime = new byte[21];
        runtime[0] = 2;
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseRuntime(runtime));
    }

    [Fact]
    public void RuntimeReportsMeasuredServiceLoad()
    {
        var runtime = new byte[21];
        runtime[0] = 1;
        BinaryPrimitives.WriteUInt16LittleEndian(runtime.AsSpan(9), 10000);
        BinaryPrimitives.WriteUInt16LittleEndian(runtime.AsSpan(11), 10000);
        BinaryPrimitives.WriteUInt32LittleEndian(runtime.AsSpan(17), 270336);
        Assert.Equal(new ushort?[] { 10000, 10000 }, Protocol.ParseRuntime(runtime).CpuUsageBasisPoints);
    }

    [Fact]
    public void StatusDecodesEveryFieldForAllSixChannels()
    {
        var status = Protocol.ParseStatus(CreateStatusPayload());

        Assert.Equal((byte)255, status.Sequence);
        Assert.Equal(6, status.Channels.Length);
        for (var index = 0; index < status.Channels.Length; index++)
        {
            Assert.Equal(new ChannelStatus(index, (byte)(index + 1), (index + 1) * 16,
                (ushort)(0x100 + index), (ushort)(0x200 + index),
                (ushort)(0x300 + index), (ushort)(0x400 + index)), status.Channels[index]);
        }
    }

    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(62)]
    [InlineData(64)]
    public void StatusRejectsWrongLengths(int length)
    {
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseStatus(new byte[length]));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(2)]
    [InlineData(15)]
    [InlineData(16)]
    [InlineData(255)]
    public void StatusRejectsUnsupportedVersions(byte version)
    {
        var payload = CreateStatusPayload();
        payload[1] = version;

        Assert.Throws<InvalidOperationException>(() => Protocol.ParseStatus(payload));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(81)]
    [InlineData(255)]
    public void StatusRejectsInvalidSignatures(byte signature)
    {
        var payload = CreateStatusPayload();
        payload[0] = signature;

        Assert.Throws<InvalidOperationException>(() => Protocol.ParseStatus(payload));
    }

    [Theory]
    [InlineData(0, 0)]
    [InlineData(2, 32)]
    [InlineData(255, 4080)]
    public void StatusScalesRingPeak(byte blocks, int expectedBytes)
    {
        var payload = CreateStatusPayload();
        payload[4] = blocks;

        Assert.Equal(expectedBytes, Protocol.ParseStatus(payload).Channels[0].RingHighWatermark);
    }

    [Fact]
    public void StatusPreservesMaximumUnsignedTrafficCounters()
    {
        var payload = CreateStatusPayload();
        payload.AsSpan(5, 8).Fill(255);
        var channel = Protocol.ParseStatus(payload).Channels[0];

        Assert.Equal(ushort.MaxValue, channel.ControllerTxBytes);
        Assert.Equal(ushort.MaxValue, channel.ControllerRxBytes);
        Assert.Equal(ushort.MaxValue, channel.CdcTxBytes);
        Assert.Equal(ushort.MaxValue, channel.CdcRxBytes);
    }

    [Theory]
    [InlineData(-32768)]
    [InlineData(-300)]
    [InlineData(0)]
    [InlineData(2530)]
    [InlineData(32767)]
    public void BoardStatusDecodesSignedTemperature(short centidegrees)
    {
        byte[] payload = [1, 0, 0, 0, 1, 2, 3, 0];
        BinaryPrimitives.WriteInt16LittleEndian(payload.AsSpan(2), centidegrees);

        var board = Protocol.ParseBoardStatus(payload);

        Assert.Equal(centidegrees / 100.0, board.TemperatureCelsius);
        Assert.Equal("1.2.3", board.FirmwareVersion);
        Assert.Equal((byte)1, board.FirmwareMajor);
        Assert.Equal((byte)2, board.FirmwareMinor);
        Assert.Equal((byte)3, board.FirmwarePatch);
    }

    [Theory]
    [InlineData(0, false)]
    [InlineData(1, true)]
    public void BoardStatusDecodesResetCapability(byte flags, bool enabled)
    {
        var board = Protocol.ParseBoardStatus([1, flags, 0, 0, 255, 0, 255, 0]);

        Assert.Equal(enabled, board.HidResetEnabled);
        Assert.Equal("255.0.255", board.FirmwareVersion);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(7)]
    [InlineData(9)]
    public void BoardStatusRejectsWrongLengths(int length)
    {
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseBoardStatus(new byte[length]));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(2)]
    [InlineData(15)]
    [InlineData(16)]
    [InlineData(255)]
    public void BoardStatusRejectsUnsupportedVersions(byte version)
    {
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseBoardStatus([version, 0, 0, 0, 1, 2, 3, 0]));
    }

    [Theory]
    [InlineData(2)]
    [InlineData(4)]
    [InlineData(8)]
    [InlineData(16)]
    [InlineData(32)]
    [InlineData(64)]
    [InlineData(128)]
    [InlineData(3)]
    public void BoardStatusRejectsUnknownCapabilityFlags(byte flags)
    {
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseBoardStatus([1, flags, 0, 0, 1, 2, 3, 0]));
    }

    [Theory]
    [InlineData(1)]
    [InlineData(128)]
    [InlineData(255)]
    public void BoardStatusRejectsReservedTrailingByte(byte reserved)
    {
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseBoardStatus([1, 0, 0, 0, 1, 2, 3, reserved]));
    }

    [Fact]
    public void OverflowsDecodeAllUnsignedLittleEndianCounters()
    {
        uint[] expected = [0, 1, 0x12345678, 0x80000000, uint.MaxValue - 1, uint.MaxValue];
        var payload = new byte[25];
        payload[0] = 1;
        for (var index = 0; index < expected.Length; index++)
            BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(1 + index * 4), expected[index]);

        Assert.Equal(expected, Protocol.ParseOverflowCounts(payload));
        Assert.Equal(expected, Protocol.ParseOverflowCounts(
            PicoUartHid.FeaturePayload([5, 5, .. payload], 5, 25, 2)));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(24)]
    [InlineData(26)]
    public void OverflowsRejectWrongLengths(int length)
    {
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseOverflowCounts(new byte[length]));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(2)]
    [InlineData(15)]
    [InlineData(16)]
    [InlineData(255)]
    public void OverflowsRejectUnsupportedVersions(byte version)
    {
        var payload = new byte[25];
        payload[0] = version;

        Assert.Throws<InvalidOperationException>(() => Protocol.ParseOverflowCounts(payload));
    }

    [Theory]
    [InlineData(1, "ready")]
    [InlineData(2, "init_failed")]
    [InlineData(4, "control_error")]
    [InlineData(8, "control_pending")]
    [InlineData(16, "cdc_open")]
    [InlineData(32, "pio")]
    [InlineData(64, "rx_overrun")]
    [InlineData(128, "rx_error")]
    public void HealthDecodesEachBitIndependently(byte health, string expected)
    {
        Assert.Equal([expected], Protocol.DecodeHealth(health));
    }

    [Fact]
    public void HealthReturnsEmptyOrAllLabelsInBitOrder()
    {
        Assert.Empty(Protocol.DecodeHealth(0));
        Assert.Equal(["ready", "init_failed", "control_error", "control_pending",
            "cdc_open", "pio", "rx_overrun", "rx_error"], Protocol.DecodeHealth(255));
    }

    [Theory]
    [InlineData(1, 0)]
    [InlineData(1, 17)]
    [InlineData(2, 0)]
    [InlineData(2, 17)]
    public void FeatureReportsStripPlatformPrefixesAndTrailingPadding(int prefixSize, int padding)
    {
        byte[] payload = [1, 0, 0xD4, 0xFE, 1, 2, 3, 0];
        var report = new byte[prefixSize + payload.Length + padding];
        report.AsSpan(0, prefixSize).Fill(3);
        payload.CopyTo(report, prefixSize);

        var decoded = PicoUartHid.FeaturePayload(report, 3, payload.Length, prefixSize);

        Assert.Equal(payload, decoded);
        report[prefixSize] = 0;
        Assert.Equal((byte)1, decoded[0]);
    }

    [Theory]
    [InlineData(1, 0)]
    [InlineData(1, 1)]
    [InlineData(1, 8)]
    [InlineData(2, 0)]
    [InlineData(2, 1)]
    [InlineData(2, 9)]
    public void FeatureReportsRejectTruncatedBuffers(int prefixSize, int length)
    {
        var report = new byte[length];
        report.AsSpan(0, Math.Min(prefixSize, length)).Fill(3);

        Assert.Throws<InvalidOperationException>(() => PicoUartHid.FeaturePayload(report, 3, 8, prefixSize));
    }

    [Theory]
    [InlineData(1)]
    [InlineData(2)]
    public void FeatureReportsRejectWrongPrimaryId(int prefixSize)
    {
        var report = new byte[8 + prefixSize];
        report[0] = 5;

        Assert.Throws<InvalidOperationException>(() => PicoUartHid.FeaturePayload(report, 3, 8, prefixSize));
    }

    [Fact]
    public void LinuxFeatureReportsRejectWrongSecondaryId()
    {
        Assert.Throws<InvalidOperationException>(() => PicoUartHid.FeaturePayload(
            [3, 5, 1, 0, 0, 0, 1, 2, 3, 0], 3, 8, 2));
    }

    [Theory]
    [InlineData(1, 125000000u, "RP2040")]
    [InlineData(1, 200000000u, "RP2040")]
    [InlineData(1, 250000000u, "RP2040")]
    [InlineData(2, 150000000u, "RP2350")]
    [InlineData(2, 280000000u, "RP2350")]
    [InlineData(0, 200000000u, "Unknown MCU (0)")]
    [InlineData(3, 200000000u, "Unknown MCU (3)")]
    [InlineData(255, uint.MaxValue, "Unknown MCU (255)")]
    public void HardwareInfoDecodesModelAndCurrentClock(byte mcu, uint clockHz, string expectedModel)
    {
        byte[] payload = [1, mcu, 0, 0, 0, 0];
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(2), clockHz);
        var expected = new HardwareInfo(expectedModel, clockHz, mcu);

        Assert.Equal(expected, Protocol.ParseHardwareInfo(payload));
        Assert.Equal(expected, Protocol.ParseHardwareInfo(PicoUartHid.FeaturePayload([6, .. payload], 6, 6, 1)));
        Assert.Equal(expected, Protocol.ParseHardwareInfo(PicoUartHid.FeaturePayload([6, 6, .. payload], 6, 6, 2)));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(5)]
    [InlineData(7)]
    public void HardwareInfoRejectsWrongLengths(int length)
    {
        Assert.Throws<InvalidOperationException>(() => Protocol.ParseHardwareInfo(new byte[length]));
    }

    [Theory]
    [InlineData(0, 1, 125000000u)]
    [InlineData(2, 1, 125000000u)]
    [InlineData(1, 1, 0u)]
    [InlineData(1, 3, 0u)]
    public void HardwareInfoRejectsUnsupportedLayoutsAndZeroClocks(byte version, byte mcu, uint clockHz)
    {
        byte[] payload = [version, mcu, 0, 0, 0, 0];
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(2), clockHz);

        Assert.Throws<InvalidOperationException>(() => Protocol.ParseHardwareInfo(payload));
    }

    private static byte[] CreateStatusPayload()
    {
        var payload = new byte[63];
        payload[0] = (byte)'P';
        payload[1] = 1;
        payload[2] = 255;
        for (var index = 0; index < 6; index++)
        {
            var channel = payload.AsSpan(3 + index * 10, 10);
            channel[0] = (byte)(index + 1);
            channel[1] = (byte)(index + 1);
            BinaryPrimitives.WriteUInt16LittleEndian(channel[2..], (ushort)(0x100 + index));
            BinaryPrimitives.WriteUInt16LittleEndian(channel[4..], (ushort)(0x200 + index));
            BinaryPrimitives.WriteUInt16LittleEndian(channel[6..], (ushort)(0x300 + index));
            BinaryPrimitives.WriteUInt16LittleEndian(channel[8..], (ushort)(0x400 + index));
        }
        return payload;
    }
}