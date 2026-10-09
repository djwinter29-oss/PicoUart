using System.Buffers.Binary;
using System.Text.Json;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class DashboardTests
{
    [Fact]
    public void InitialSnapshotHasSixUnknownChannelsAndNoInventedMetadata()
    {
        var snapshot = new DashboardSnapshot();

        Assert.False(snapshot.Connected);
        Assert.Null(snapshot.Error);
        Assert.Null(snapshot.MetadataError);
        Assert.Null(snapshot.HardwareError);
        Assert.Null(snapshot.Hardware);
        Assert.Null(snapshot.UpdatedAt);
        Assert.Null(snapshot.Sequence);
        Assert.Null(snapshot.Board);
        Assert.False(snapshot.TrafficIncomplete);
        Assert.Equal(6, snapshot.OverflowCounts.Length);
        Assert.All(snapshot.OverflowCounts, count => Assert.Null(count));
        Assert.Equal(Enumerable.Range(0, 6), snapshot.Channels.Select(channel => channel.Id));
        Assert.All(snapshot.Channels, channel =>
        {
            Assert.Null(channel.Health);
            Assert.Empty(channel.HealthLabels);
            Assert.Equal("unknown", channel.State);
            Assert.Equal("unknown", channel.Backend);
            Assert.False(channel.CdcOpen);
            Assert.Equal(0, channel.RingHighWatermark);
            AssertTraffic(channel.Traffic, 0, 0, 0, 0);
            AssertTraffic(channel.Totals, 0, 0, 0, 0);
        });
    }

    [Theory]
    [InlineData(0, "initializing", "Hardware", false)]
    [InlineData(1, "ready", "Hardware", false)]
    [InlineData(8, "initializing", "Hardware", false)]
    [InlineData(9, "ready", "Hardware", false)]
    [InlineData(17, "ready", "Hardware", true)]
    [InlineData(33, "ready", "PIO", false)]
    [InlineData(49, "ready", "PIO", true)]
    [InlineData(2, "attention", "Hardware", false)]
    [InlineData(3, "attention", "Hardware", false)]
    [InlineData(5, "attention", "Hardware", false)]
    [InlineData(65, "attention", "Hardware", false)]
    [InlineData(129, "attention", "Hardware", false)]
    [InlineData(255, "attention", "PIO", true)]
    public void TelemetryMapsHealthAndBackendForEveryChannel(
        byte health, string state, string backend, bool cdcOpen)
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(CreateStatus(0, health));

        Assert.All(snapshot.Channels, channel =>
        {
            Assert.Equal(health, channel.Health);
            Assert.Equal(Protocol.DecodeHealth(health), channel.HealthLabels);
            Assert.Equal(state, channel.State);
            Assert.Equal(backend, channel.Backend);
            Assert.Equal(cdcOpen, channel.CdcOpen);
            Assert.Equal(32, channel.RingHighWatermark);
        });
    }

    [Fact]
    public void TelemetryClearsConnectionErrorsButPreservesMetadataErrors()
    {
        var snapshot = new DashboardSnapshot
        {
            Error = "disconnected", MetadataError = "feature read failed", HardwareError = "report unsupported"
        };
        var before = DateTimeOffset.UtcNow;

        snapshot.Apply(CreateStatus(42));

        Assert.True(snapshot.Connected);
        Assert.Null(snapshot.Error);
        Assert.Equal("feature read failed", snapshot.MetadataError);
        Assert.Equal("report unsupported", snapshot.HardwareError);
        Assert.Equal((byte)42, snapshot.Sequence);
        Assert.NotNull(snapshot.UpdatedAt);
        Assert.InRange(snapshot.UpdatedAt.Value, before, DateTimeOffset.UtcNow);
        Assert.Null(snapshot.Board);
        Assert.All(snapshot.OverflowCounts, count => Assert.Null(count));
    }

    [Fact]
    public void TrafficDeltasAreReplacedAndTotalsAccumulateIndependentlyForAllChannels()
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(CreateStatus(0));
        var firstTraffic = snapshot.Channels[0].Traffic;
        var second = new Status(1, Enumerable.Range(0, 6).Select(index =>
            new ChannelStatus(index, 0, 64, 100, 200, 300, 400)).ToArray());

        snapshot.Apply(second);

        Assert.NotSame(firstTraffic, snapshot.Channels[0].Traffic);
        for (var index = 0; index < 6; index++)
        {
            var channel = snapshot.Channels[index];
            AssertTraffic(channel.Traffic, 100, 200, 300, 400);
            AssertTraffic(channel.Totals, 110 + index, 220 + index, 330 + index, 440 + index);
            Assert.Equal("initializing", channel.State);
            Assert.Equal(64, channel.RingHighWatermark);
        }
    }

    [Theory]
    [InlineData(0, 1, false)]
    [InlineData(254, 255, false)]
    [InlineData(255, 0, false)]
    [InlineData(0, 0, true)]
    [InlineData(1, 0, true)]
    [InlineData(0, 2, true)]
    [InlineData(255, 1, true)]
    public void SequenceTracksWrapGapsDuplicatesAndBackwardsReports(byte first, byte second, bool incomplete)
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(CreateStatus(first));
        Assert.False(snapshot.TrafficIncomplete);

        snapshot.Apply(CreateStatus(second));

        Assert.Equal(incomplete, snapshot.TrafficIncomplete);
    }

    [Fact]
    public void TrafficIncompleteRemainsSetAfterLaterConsecutiveReports()
    {
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(CreateStatus(0));
        snapshot.Apply(CreateStatus(2));
        snapshot.Apply(CreateStatus(3));

        Assert.True(snapshot.TrafficIncomplete);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    public void EachSaturatedDirectionMarksTrafficIncompleteOnTheLastChannel(int direction)
    {
        var status = CreateStatus(0);
        ushort[] counts = [10, 20, 30, 40];
        counts[direction] = ushort.MaxValue;
        status.Channels[5] = new(5, 1, 32, counts[0], counts[1], counts[2], counts[3]);
        var snapshot = new DashboardSnapshot();

        snapshot.Apply(status);

        Assert.True(snapshot.TrafficIncomplete);
        AssertTraffic(snapshot.Channels[5].Totals, counts[0], counts[1], counts[2], counts[3]);
    }

    [Fact]
    public void TrafficTotalsDoNotWrapAtTheReportCounterWidth()
    {
        var status = CreateStatus(0);
        status.Channels[0] = new(0, 1, 32, 60000, 60000, 60000, 60000);
        var snapshot = new DashboardSnapshot();
        snapshot.Apply(status);
        snapshot.Apply(status with { Sequence = 1 });

        AssertTraffic(snapshot.Channels[0].Totals, 120000, 120000, 120000, 120000);
        Assert.False(snapshot.TrafficIncomplete);
    }

    [Fact]
    public void JsonPreservesTheSharedWebContractAndNullUnknownValues()
    {
        using var service = new DashboardService(null, null);
        using var initial = JsonDocument.Parse(service.SnapshotJson());
        Assert.Equal(new[] { "board", "channels", "connected", "error", "hardware", "hardware_error",
            "metadata_error", "overflow_counts",
            "sequence", "traffic_incomplete", "updated_at" },
            initial.RootElement.EnumerateObject().Select(property => property.Name).Order());
        Assert.Equal(JsonValueKind.Null, initial.RootElement.GetProperty("board").ValueKind);
        Assert.Equal(JsonValueKind.Null, initial.RootElement.GetProperty("hardware").ValueKind);
        Assert.Equal(JsonValueKind.Null, initial.RootElement.GetProperty("overflow_counts")[0].ValueKind);

        var snapshot = new DashboardSnapshot
        {
            Board = new(25.3, "1.2.3", 1, 2, 3, false),
            Hardware = new("RP2350", 280000000, 2),
            OverflowCounts = [0, 1, 2, 3, 4, uint.MaxValue]
        };
        snapshot.Apply(CreateStatus(7));
        using var populated = JsonDocument.Parse(JsonSerializer.Serialize(snapshot, Protocol.JsonOptions));
        var board = populated.RootElement.GetProperty("board");
        Assert.Equal(new[] { "firmware_major", "firmware_minor", "firmware_patch", "firmware_version",
            "hid_reset_enabled", "temperature_celsius" }, board.EnumerateObject().Select(property => property.Name).Order());
        Assert.Equal("1.2.3", board.GetProperty("firmware_version").GetString());
        Assert.False(board.GetProperty("hid_reset_enabled").GetBoolean());
        var hardware = populated.RootElement.GetProperty("hardware");
        Assert.Equal("RP2350", hardware.GetProperty("mcu").GetString());
        Assert.Equal(2, hardware.GetProperty("mcu_id").GetByte());
        Assert.Equal(280000000u, hardware.GetProperty("system_clock_hz").GetUInt32());
        Assert.Equal(uint.MaxValue, populated.RootElement.GetProperty("overflow_counts")[5].GetUInt32());
        var channel = populated.RootElement.GetProperty("channels")[0];
        Assert.Equal(new[] { "backend", "cdc_open", "health", "health_labels", "id", "ring_high_watermark",
            "state", "totals", "traffic" }, channel.EnumerateObject().Select(property => property.Name).Order());
        Assert.Equal(new[] { "uart_rx", "uart_tx", "usb_rx", "usb_tx" },
            channel.GetProperty("totals").EnumerateObject().Select(property => property.Name).Order());
        Assert.Equal(10, channel.GetProperty("totals").GetProperty("uart_tx").GetInt64());
    }

    [Theory]
    [InlineData("toggle-led")]
    [InlineData("reset")]
    public void ControlsFailClearlyWithoutAnOpenConnection(string action)
    {
        using var service = new DashboardService(null, null);

        var error = Assert.Throws<InvalidOperationException>(() => service.Action(action));

        Assert.Equal("PicoUart HID interface is not connected", error.Message);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(3)]
    [InlineData(255)]
    public void UnknownMcuKeepsItsNumericIdAndClockInDashboardJson(byte mcuId)
    {
        byte[] payload = [1, mcuId, 0, 0, 0, 0];
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(2), 200000000);
        var snapshot = new DashboardSnapshot { Hardware = Protocol.ParseHardwareInfo(payload) };
        snapshot.Apply(CreateStatus(0));

        using var json = JsonDocument.Parse(JsonSerializer.Serialize(snapshot, Protocol.JsonOptions));
        var hardware = json.RootElement.GetProperty("hardware");
        Assert.Equal($"Unknown MCU ({mcuId})", hardware.GetProperty("mcu").GetString());
        Assert.Equal(mcuId, hardware.GetProperty("mcu_id").GetByte());
        Assert.Equal(200000000u, hardware.GetProperty("system_clock_hz").GetUInt32());
        Assert.Equal(JsonValueKind.Null, json.RootElement.GetProperty("hardware_error").ValueKind);
    }

    private static Status CreateStatus(byte sequence, byte health = 1) => new(sequence,
        Enumerable.Range(0, 6).Select(index => new ChannelStatus(index, health, 32,
            (ushort)(10 + index), (ushort)(20 + index), (ushort)(30 + index), (ushort)(40 + index))).ToArray());

    private static void AssertTraffic(Traffic traffic, long uartTx, long uartRx, long usbTx, long usbRx)
    {
        Assert.Equal(uartTx, traffic.UartTx);
        Assert.Equal(uartRx, traffic.UartRx);
        Assert.Equal(usbTx, traffic.UsbTx);
        Assert.Equal(usbRx, traffic.UsbRx);
    }
}