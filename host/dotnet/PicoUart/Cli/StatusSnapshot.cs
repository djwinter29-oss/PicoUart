// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Globalization;
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
