// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;
using PicoUart.Wire;

namespace PicoUart.Cli;

/// <summary>One CLI status snapshot. Shape matches Python <c>status --json</c>.</summary>
internal sealed record StatusSnapshot(StatusChannel[] Channels)
{
    internal static StatusSnapshot FromFirstSample(ChannelStatus sample, DateTimeOffset observedAt)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(sample.Id);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(sample.Id, Protocol.ChannelCount);
        var channels = new StatusChannel[Protocol.ChannelCount];
        for (var index = 0; index < channels.Length; index++)
            channels[index] = StatusChannel.Waiting(index);
        channels[sample.Id] = StatusChannel.FirstSample(sample, observedAt);
        return new(channels);
    }

    /// <summary>Retry until one Input arrives. Each read waits at most 250 ms, matching Python status.</summary>
    internal static ChannelStatus? Poll(Func<int, ChannelStatus?> read, TimeSpan timeout)
    {
        ArgumentOutOfRangeException.ThrowIfLessThanOrEqual(timeout, TimeSpan.Zero);
        var clock = Stopwatch.StartNew();
        while (clock.Elapsed < timeout)
        {
            var remainingMs = (timeout - clock.Elapsed).TotalMilliseconds;
            if (remainingMs <= 0) break;
            var slice = (int)Math.Min(250, Math.Max(1, remainingMs));
            var sample = read(slice);
            if (sample is not null) return sample;
        }
        return null;
    }

    /// <summary>Serialize with recursively sorted object keys, matching Python <c>sort_keys=True</c>.</summary>
    internal static string ToSortedJson(StatusSnapshot snapshot)
    {
        var node = JsonSerializer.SerializeToNode(snapshot, Protocol.JsonOptions)
            ?? throw new InvalidOperationException("status snapshot did not serialize");
        SortKeys(node);
        return node.ToJsonString(Protocol.JsonOptions);
    }

    private static void SortKeys(JsonNode? node)
    {
        switch (node)
        {
            case JsonObject obj:
                var entries = obj.ToArray();
                foreach (var entry in entries)
                    obj.Remove(entry.Key);
                foreach (var entry in entries.OrderBy(entry => entry.Key, StringComparer.Ordinal))
                {
                    SortKeys(entry.Value);
                    obj.Add(entry.Key, entry.Value);
                }
                break;
            case JsonArray array:
                foreach (var item in array)
                    SortKeys(item);
                break;
        }
    }
}

internal sealed record StatusBytes(uint? UartTx, uint? UartRx, uint? UsbTx, uint? UsbRx)
{
    internal static StatusBytes Unknown { get; } = new(null, null, null, null);

    internal static StatusBytes Zero { get; } = new(0, 0, 0, 0);
}

internal sealed record StatusChannel(
    int Id,
    string? UpdatedAt,
    int? Sequence,
    int ObservedIntervalMs,
    int TrafficGaps,
    bool TrafficIncomplete,
    string State,
    int? ChannelState,
    int? UartMeasurementState,
    bool CdcOpen,
    StatusBytes Traffic,
    StatusBytes Totals,
    int? IntervalMs,
    uint? BaudRate,
    int? DataBits,
    int? StopBits,
    string? Parity)
{
    private static readonly string[] States = ["closed", "starting", "ready", "configuring", "error"];

    internal static StatusChannel Waiting(int id) => new(
        id, null, null, 0, 0, false, "unknown", null, null, false,
        StatusBytes.Unknown, StatusBytes.Zero, null, null, null, null, null);

    internal static StatusChannel FirstSample(ChannelStatus sample, DateTimeOffset observedAt)
    {
        var state = Array.IndexOf(States, sample.State);
        if (state < 0) throw new ArgumentException("invalid channel state", nameof(sample));
        var openBaud = sample.BaudRate != 0;
        return new(
            sample.Id,
            observedAt.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss.fff'+00:00'", CultureInfo.InvariantCulture),
            sample.Sequence,
            0,
            0,
            sample.IntervalMs is 0 or ushort.MaxValue,
            sample.State,
            state,
            sample.UartMeasurementState,
            state != 0,
            StatusBytes.Unknown,
            new(sample.UartTxBytes, sample.UartRxBytes, sample.CdcTxBytes, sample.CdcRxBytes),
            sample.IntervalMs,
            sample.BaudRate,
            openBaud ? sample.DataBits : null,
            openBaud ? sample.StopBits : null,
            openBaud ? sample.Parity : null);
    }
}
