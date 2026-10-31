// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Text.Json;
using PicoUart.Cli;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class StatusSnapshotTests
{
    private static readonly DateTimeOffset Observed = new(2026, 10, 30, 22, 15, 1, 250, TimeSpan.Zero);

    [Fact]
    public void FirstSampleMatchesThePythonStatusObject()
    {
        var sample = new ChannelStatus(2, "ready", 1, 115200, 8, 1, "none", 9, 500, 100, 200, 300, 400);
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(
            StatusSnapshot.FromFirstSample(sample, Observed), Protocol.JsonOptions));
        var channels = json.RootElement.GetProperty("channels");
        Assert.Equal(6, channels.GetArrayLength());

        var waiting = channels[0];
        Assert.Equal(0, waiting.GetProperty("id").GetInt32());
        Assert.Equal(JsonValueKind.Null, waiting.GetProperty("sequence").ValueKind);
        Assert.Equal("unknown", waiting.GetProperty("state").GetString());
        Assert.Equal(JsonValueKind.Null, waiting.GetProperty("updated_at").ValueKind);
        Assert.Equal(JsonValueKind.Null, waiting.GetProperty("traffic").GetProperty("uart_tx").ValueKind);
        Assert.Equal(0u, waiting.GetProperty("totals").GetProperty("usb_rx").GetUInt32());
        Assert.False(waiting.GetProperty("traffic_incomplete").GetBoolean());
        Assert.False(waiting.GetProperty("cdc_open").GetBoolean());

        var live = channels[2];
        Assert.Equal("2026-10-30T22:15:01.250+00:00", live.GetProperty("updated_at").GetString());
        Assert.Equal(9, live.GetProperty("sequence").GetInt32());
        Assert.Equal(0, live.GetProperty("observed_interval_ms").GetInt32());
        Assert.Equal("ready", live.GetProperty("state").GetString());
        Assert.Equal(2, live.GetProperty("channel_state").GetInt32());
        Assert.True(live.GetProperty("cdc_open").GetBoolean());
        Assert.Equal(115200u, live.GetProperty("baud_rate").GetUInt32());
        Assert.Equal(JsonValueKind.Null, live.GetProperty("traffic").GetProperty("uart_tx").ValueKind);
        Assert.Equal(JsonValueKind.Null, live.GetProperty("traffic").GetProperty("uart_rx").ValueKind);
        Assert.Equal(JsonValueKind.Null, live.GetProperty("traffic").GetProperty("usb_tx").ValueKind);
        Assert.Equal(JsonValueKind.Null, live.GetProperty("traffic").GetProperty("usb_rx").ValueKind);
        Assert.Equal(100u, live.GetProperty("totals").GetProperty("uart_tx").GetUInt32());
        Assert.Equal(200u, live.GetProperty("totals").GetProperty("uart_rx").GetUInt32());
        Assert.Equal(300u, live.GetProperty("totals").GetProperty("usb_tx").GetUInt32());
        Assert.Equal(400u, live.GetProperty("totals").GetProperty("usb_rx").GetUInt32());
        Assert.False(live.GetProperty("traffic_incomplete").GetBoolean());
        Assert.Equal(500, live.GetProperty("interval_ms").GetInt32());
    }

    [Theory]
    [InlineData(0)]
    [InlineData(65535)]
    public void FirstSampleMarksAnUnusableIntervalIncomplete(int interval)
    {
        var sample = new ChannelStatus(0, "ready", 1, 115200, 8, 1, "none", 1, (ushort)interval, 1, 2, 3, 4);
        var channel = StatusSnapshot.FromFirstSample(sample, Observed).Channels[0];
        Assert.True(channel.TrafficIncomplete);
        Assert.Equal(0, channel.ObservedIntervalMs);
        Assert.Equal(0, channel.TrafficGaps);
        Assert.Null(channel.Traffic.UartTx);
    }

    [Fact]
    public void ClosedZeroBaudSampleKeepsFramingUnset()
    {
        var sample = new ChannelStatus(1, "closed", 0, 0, 0, 0, null, 4, 500, 0, 0, 0, 0);
        var channel = StatusSnapshot.FromFirstSample(sample, Observed).Channels[1];
        Assert.Equal(0u, channel.BaudRate);
        Assert.Null(channel.DataBits);
        Assert.Null(channel.StopBits);
        Assert.Null(channel.Parity);
        Assert.Equal(0, channel.ChannelState);
        Assert.False(channel.CdcOpen);
        Assert.Equal("unknown", StatusSnapshot.FromFirstSample(sample, Observed).Channels[0].State);
    }

    [Fact]
    public void StatusJsonSortsObjectKeys()
    {
        var sample = new ChannelStatus(2, "ready", 1, 115200, 8, 1, "none", 9, 500, 100, 200, 300, 400);
        var json = StatusSnapshot.ToSortedJson(StatusSnapshot.FromFirstSample(sample, Observed));
        using var document = JsonDocument.Parse(json);
        AssertSorted(document.RootElement);
        var live = document.RootElement.GetProperty("channels")[2];
        Assert.Equal("baud_rate", live.EnumerateObject().First().Name);
        Assert.Equal("uart_rx", live.GetProperty("traffic").EnumerateObject().First().Name);
        Assert.Equal("uart_rx", live.GetProperty("totals").EnumerateObject().First().Name);
    }

    [Fact]
    public void PollReturnsTheFirstSampleAndCapsEachReadAt250Ms()
    {
        var calls = new List<int>();
        var sample = new ChannelStatus(0, "ready", 1, 115200, 8, 1, "none", 1, 500, 1, 2, 3, 4);
        ChannelStatus? found = null;
        var reads = 0;
        found = StatusSnapshot.Poll(timeoutMs =>
        {
            calls.Add(timeoutMs);
            return ++reads == 3 ? sample : null;
        }, TimeSpan.FromMilliseconds(800));
        Assert.Same(sample, found);
        Assert.Equal(3, calls.Count);
        Assert.All(calls, timeoutMs => Assert.InRange(timeoutMs, 1, 250));
    }

    [Fact]
    public void PollReturnsNullWhenEveryReadIsEmpty()
    {
        var calls = 0;
        var found = StatusSnapshot.Poll(_ =>
        {
            calls++;
            return null;
        }, TimeSpan.FromMilliseconds(30));
        Assert.Null(found);
        Assert.True(calls >= 1);
    }

    private static void AssertSorted(JsonElement element)
    {
        switch (element.ValueKind)
        {
            case JsonValueKind.Object:
                var names = element.EnumerateObject().Select(property => property.Name).ToArray();
                Assert.Equal(names.OrderBy(name => name, StringComparer.Ordinal), names);
                foreach (var property in element.EnumerateObject())
                    AssertSorted(property.Value);
                break;
            case JsonValueKind.Array:
                foreach (var item in element.EnumerateArray())
                    AssertSorted(item);
                break;
        }
    }
}
