// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Buffers.Binary;
using PicoUart.Protocol;
using Xunit;

namespace PicoUart.Tests.Protocol;

[Trait("Category", "Unit")]
public sealed class CodecTests
{
    [Fact]
    public void BuildActionUsesReport20Layout()
    {
        var identity = ProtocolFixtures.Identity();
        Assert.Equal(new byte[] { 1, 1, 0, 0 }, Codec.BuildAction(1, identity));
        Assert.Equal(new byte[] { 1, 3, 1, 0 }, Codec.BuildAction(3, identity));
        Assert.Equal(new byte[] { 1, 4, 3, 4 }, Codec.BuildAction(4, identity, 4, true));
    }

    [Fact]
    public void SaveRequiresCapabilityProfileAndAcknowledgement()
    {
        var identity = ProtocolFixtures.Identity();
        Assert.Throws<ArgumentException>(() => Codec.BuildAction(4, identity, 3, false));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            Codec.BuildAction(4, identity with { ClockProfileMask = 0x0F }, 4, true));
        Assert.Throws<InvalidOperationException>(() =>
            Codec.BuildAction(4, identity with { Capabilities = 3 }, 0, false));
    }

    [Fact]
    public void IdentityParsesSelectedLayout()
    {
        var identity = ProtocolFixtures.Identity();
        Assert.Equal(1, identity.ProductId);
        Assert.Equal("PicoUart", identity.ProductName);
        Assert.Equal("0123456789ABCDEF", identity.DeviceUid);
        Assert.Equal("1.2.3", identity.FirmwareVersion);
        Assert.Equal("RP2350", identity.Mcu);
        Assert.Equal("Pico 2", identity.BoardModel);
        Assert.Equal((byte)7, identity.Capabilities);
        Assert.Equal((byte)0x1f, identity.ClockProfileMask);
        Assert.Equal((uint)532480, identity.RamTotalBytes);
        Assert.Equal((uint)65536, identity.RamReservedBytes);
    }

    [Theory]
    [InlineData(0, 2)]
    [InlineData(17, 0x80)]
    [InlineData(18, 0)]
    public void IdentityRejectsMalformedFields(int offset, byte value)
    {
        var data = ProtocolFixtures.IdentityPayload();
        data[offset] = value;
        Assert.Throws<InvalidOperationException>(() => Codec.ParseIdentity(data));
    }

    [Fact]
    public void ConfigurationParsesPinsAndCapacities()
    {
        var configuration = ProtocolFixtures.Configuration();
        Assert.Equal((byte)2, configuration.StoredProfile);
        Assert.Equal((byte)6, configuration.ChannelCount);
        Assert.Equal((byte)0, configuration.ChannelPins[0].TxPin);
        Assert.Null(configuration.ChannelPins[2].RtsPin);
        Assert.Equal((ushort)4096, configuration.UartTxRingCapacityBytes);
        Assert.Equal((ushort)2048, configuration.CdcRxFifoCapacityBytes);
    }

    [Fact]
    public void ConfigurationPreservesKnownAndUnassignedResetCauses()
    {
        foreach (var cause in Enumerable.Range(0, 11).Append(255))
        {
            var data = ProtocolFixtures.ConfigurationPayload();
            data[27] = (byte)cause;
            Assert.Equal((byte)cause, Codec.ParseConfiguration(data, ProtocolFixtures.Identity()).ResetCause);
        }
    }

    [Fact]
    public void ConfigurationAcceptsRetained280MhzRequestOnRp2040()
    {
        var identity = ProtocolFixtures.Identity() with { McuId = 1, ClockProfileMask = 0x0f };
        var data = ProtocolFixtures.ConfigurationPayload();
        data[1] = 4;

        var configuration = Codec.ParseConfiguration(data, identity);

        Assert.Equal((byte)4, configuration.StoredProfile);
    }

    [Fact]
    public void ConfigurationRejectsUnassignedStoredProfile()
    {
        var data = ProtocolFixtures.ConfigurationPayload();
        data[1] = 5;

        Assert.Throws<InvalidOperationException>(() => Codec.ParseConfiguration(data, ProtocolFixtures.Identity()));
    }

    [Fact]
    public void TelemetryParsesServiceLoadAndClock()
    {
        var data = new byte[20];
        data[0] = 1;
        data[1] = 1;
        BinaryPrimitives.WriteInt16LittleEndian(data.AsSpan(2), -125);
        BinaryPrimitives.WriteUInt64LittleEndian(data.AsSpan(4), 4294967296ul);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(12), 10000);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(14), ushort.MaxValue);
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(16), 280000000);
        var telemetry = Codec.ParseTelemetry(data, ProtocolFixtures.Identity());
        Assert.True(telemetry.Led.OutputOn);
        Assert.Equal(-1.25, telemetry.TemperatureCelsius);
        Assert.Equal(4294967296ul, telemetry.Runtime.UptimeMs);
        Assert.Equal((ushort)10000, telemetry.Runtime.ServiceLoadBasisPoints[0]);
        Assert.Null(telemetry.Runtime.ServiceLoadBasisPoints[1]);
        Assert.Equal((uint)280000000, telemetry.SystemClockHz);
    }

    [Fact]
    public void ChannelParsesStateMeasurementAndCounters()
    {
        var data = new byte[30];
        data[0] = 1;
        data[1] = 2;
        data[2] = 1;
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(3), 115200);
        data[7] = 8; data[8] = 1; data[9] = 0;
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(10), 12);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(12), 500);
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(14), 100);
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(18), 200);
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(22), 300);
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(26), 400);
        var channel = Codec.ParseChannel(10, data, ProtocolFixtures.Identity());
        Assert.Equal(0, channel.Id);
        Assert.Equal("ready", channel.State);
        Assert.Equal((byte)1, channel.UartMeasurementState);
        Assert.Equal((uint)115200, channel.BaudRate);
        Assert.Equal("none", channel.Parity);
        Assert.Equal((uint)400, channel.CdcRxBytes);
    }

    [Fact]
    public void ChannelRequiresZeroUartCountersWhenMeasurementIsUnavailable()
    {
        var data = new byte[30];
        data[0] = 1;
        data[1] = 2;
        data[2] = 0;
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(3), 115200);
        data[7] = 8; data[8] = 1;
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(14), 1);

        Assert.Throws<InvalidOperationException>(() =>
            Codec.ParseChannel(10, data, ProtocolFixtures.Identity()));

        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(14), 0);
        var channel = Codec.ParseChannel(10, data, ProtocolFixtures.Identity());
        Assert.Equal((byte)0, channel.UartMeasurementState);
        Assert.Equal((uint)0, channel.UartTxBytes);
    }

    [Theory]
    [InlineData(0x40)]
    [InlineData(0x80)]
    [InlineData(0xc3)]
    public void ChannelRejectsHardwareMaskBitsOutsideSupportedChannels(byte hardwareMask)
    {
        var data = new byte[30];
        data[0] = 1;
        data[1] = 2;
        data[2] = 1;
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(3), 115200);
        data[7] = 8;
        data[8] = 1;
        var identity = ProtocolFixtures.Identity() with { HardwareChannelMask = hardwareMask };

        Assert.Throws<InvalidOperationException>(() => Codec.ParseChannel(10, data, identity));
        Assert.Throws<InvalidOperationException>(() =>
            Codec.ParseConfiguration(ProtocolFixtures.ConfigurationPayload(), identity));
    }

    [Fact]
    public void DiagnosticsParsesAndValidatesCapacity()
    {
        var data = new byte[24];
        data[0] = 1;
        data[1] = 1;
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(4), 5);
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(8), 7);
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(12), 9);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(16), 4096);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(18), 4095);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(20), 2048);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(22), 2047);
        var diagnostics = Codec.ParseDiagnostics(30, data, ProtocolFixtures.Configuration());
        Assert.Equal((uint)5, diagnostics.RxOverrunCount);
        Assert.Equal((uint)9, diagnostics.LastFaultUptimeS);
    }

    [Fact]
    public void ActionResultAndBuilderEnforceContract()
    {
        Assert.Equal(new ActionResult(4, 2, 0), Codec.ParseActionResult([1, 4, 2, 0]));
        Assert.Throws<InvalidOperationException>(() => Codec.ParseActionResult([1, 4, 2, 3]));
        Assert.Equal(new byte[] { 1, 4, 3, 4 }, Codec.BuildAction(4, ProtocolFixtures.Identity(), 4, true));
    }
}
