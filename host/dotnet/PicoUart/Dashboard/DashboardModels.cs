// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Diagnostics;
using PicoUart.Wire;

namespace PicoUart.Dashboard;

public sealed class Traffic
{
    public double? UartTx { get; set; }
    public double? UartRx { get; set; }
    public double? UsbTx { get; set; }
    public double? UsbRx { get; set; }
}

public sealed class DashboardChannel(int id)
{
    public int Id { get; } = id;
    public string State { get; set; } = "unknown";
    public byte? ChannelState { get; set; }
    public bool CdcOpen { get; set; }
    public byte? UartMeasurementState { get; set; }
    public uint? BaudRate { get; set; }
    public byte? DataBits { get; set; }
    public byte? StopBits { get; set; }
    public string? Parity { get; set; }
    public Traffic Traffic { get; set; } = new();
    public Traffic Totals { get; } = new() { UartTx = 0, UartRx = 0, UsbTx = 0, UsbRx = 0 };
    public DateTimeOffset? UpdatedAt { get; set; }
    public ushort? Sequence { get; set; }
    public ushort? IntervalMs { get; set; }
    public ulong ObservedIntervalMs { get; set; }
    public long TrafficGaps { get; set; }
    public bool TrafficIncomplete { get; set; }
    internal Traffic? CounterBaseline { get; set; }
    internal byte? MeasurementBaseline { get; set; }
    internal long LastInputTimestamp { get; set; }
    internal bool Stale { get; set; }
}

public sealed record BoardControlStatus(byte Action, byte Status, byte ErrorReason, string? Error);

public sealed class DashboardSnapshot
{
    public bool Connected { get; set; }
    public int RefreshIntervalMs { get; private set; } = 500;
    public string? Error { get; set; }
    public string? MetadataError { get; set; }
    public string? HardwareError { get; set; }
    public HardwareInfo? Hardware { get; set; }
    public ConfigurationStatus? Configuration { get; set; }
    public string? ConfigurationError { get; set; }
    public ConfigurationSaveStatus? ConfigurationSave { get; set; }
    public string? ConfigurationSaveError { get; set; }
    public BoardIdentity? Identity { get; set; }
    public string? IdentityError { get; set; }
    public RuntimeStatus? Runtime { get; set; }
    public string? RuntimeError { get; set; }
    public LedStatus? Led { get; set; }
    public string? LedError { get; set; }
    public DateTimeOffset? UpdatedAt { get; set; }
    public bool TrafficIncomplete => Channels.Any(channel => channel.TrafficIncomplete);
    public long TrafficGaps => Channels.Sum(channel => channel.TrafficGaps);
    public ChannelDiagnostics?[] ChannelDiagnostics { get; } = new ChannelDiagnostics?[Protocol.ChannelCount];
    public string? DiagnosticsError { get; set; }
    public BoardStatus? Board { get; set; }
    public uint?[] OverflowCounts => ChannelDiagnostics.Select(diagnostic => diagnostic?.RxOverrunCount).ToArray();
    internal BoardControlStatus? Control { get; set; }
    public DashboardChannel[] Channels { get; } = Enumerable.Range(0, Protocol.ChannelCount)
        .Select(index => new DashboardChannel(index)).ToArray();

    internal void SetRuntimeReadResult(RuntimeStatus? runtime, string? error)
    {
        Runtime = runtime;
        RuntimeError = error;
    }

    internal void ApplyAction(ActionResult result)
    {
        Control = new(result.Action, result.Status, result.ErrorReason,
            result.Status is >= 3 and <= 6
                ? $"board action {Protocol.ActionStatusName(result.Status)} (error {result.ErrorReason})"
                : null);
        if (result.Action == 4)
        {
            ConfigurationSave = new(result.Status, result.ErrorReason, null);
            ConfigurationSaveError = Control.Error;
        }
    }

    /// <summary>Apply on-demand state without advancing interrupt traffic or freshness.</summary>
    public void ApplyFeature(ChannelStatus source) => ApplyState(source);

    public void Apply(ChannelStatus source)
    {
        var channel = Channels[source.Id];
        var now = Stopwatch.GetTimestamp();
        var first = channel.Sequence is null;
        var stale = !first && (channel.Stale
            || Stopwatch.GetElapsedTime(channel.LastInputTimestamp, now).TotalSeconds >= 3);
        if (stale)
        {
            if (!channel.Stale) MarkIncomplete(channel);
            first = true;
        }
        var advance = first ? 1 : (source.Sequence - channel.Sequence!.Value + 65536) & 65535;
        if (!first && (advance == 0 || advance >= 32768))
        {
            MarkIncomplete(channel);
            return;
        }
        var exact = source.IntervalMs is > 0 and < ushort.MaxValue;
        var gap = !first && (source.Sequence != (ushort)((channel.Sequence!.Value + 1) & 65535)
            || Stopwatch.GetElapsedTime(channel.LastInputTimestamp, now).TotalSeconds >= 3);
        var previous = channel.CounterBaseline;
        var previousMeasurement = channel.MeasurementBaseline;
        if (!first && ((gap && !channel.Stale) || !exact))
            MarkIncomplete(channel);
        Connected = true;
        Error = null;
        if (exact) RefreshIntervalMs = Math.Clamp((int)source.IntervalMs, 500, 1000);
        ApplyState(source);
        channel.Sequence = source.Sequence;
        channel.IntervalMs = source.IntervalMs;
        channel.UpdatedAt = UpdatedAt = DateTimeOffset.UtcNow;
        channel.LastInputTimestamp = now;
        channel.Stale = false;
        var counters = new Traffic
        {
            UartTx = source.UartTxBytes,
            UartRx = source.UartRxBytes,
            UsbTx = source.CdcTxBytes,
            UsbRx = source.CdcRxBytes
        };
        channel.Totals.UartTx = source.UartTxBytes;
        channel.Totals.UartRx = source.UartRxBytes;
        channel.Totals.UsbTx = source.CdcTxBytes;
        channel.Totals.UsbRx = source.CdcRxBytes;
        channel.Traffic = new();
        if (!first && !gap && exact && previous is not null)
        {
            var uartCurrent = source.UartMeasurementState == 1 && previousMeasurement == 1;
            var scale = 1000.0 / source.IntervalMs;
            channel.Traffic = new()
            {
                UartTx = uartCurrent ? Delta(source.UartTxBytes, previous.UartTx) * scale : null,
                UartRx = uartCurrent ? Delta(source.UartRxBytes, previous.UartRx) * scale : null,
                UsbTx = Delta(source.CdcTxBytes, previous.UsbTx) * scale,
                UsbRx = Delta(source.CdcRxBytes, previous.UsbRx) * scale
            };
            channel.ObservedIntervalMs += source.IntervalMs;
        }
        channel.CounterBaseline = counters;
        channel.MeasurementBaseline = source.UartMeasurementState;
    }

    public void MarkStale()
    {
        var now = Stopwatch.GetTimestamp();
        foreach (var channel in Channels)
        {
            if (channel.Sequence is null || channel.Stale
                || Stopwatch.GetElapsedTime(channel.LastInputTimestamp, now).TotalSeconds < 3) continue;
            channel.Stale = true;
            MarkIncomplete(channel);
            channel.State = "unknown";
            channel.Traffic = new();
        }
    }

    private void ApplyState(ChannelStatus source)
    {
        var channel = Channels[source.Id];
        channel.State = source.State;
        channel.ChannelState = source.State switch
        {
            "closed" => 0, "starting" => 1, "ready" => 2, "configuring" => 3, "error" => 4,
            _ => throw new ArgumentException("invalid channel state", nameof(source))
        };
        channel.CdcOpen = channel.ChannelState != 0;
        channel.UartMeasurementState = source.UartMeasurementState;
        channel.BaudRate = source.BaudRate;
        channel.DataBits = source.BaudRate == 0 ? null : source.DataBits;
        channel.StopBits = source.BaudRate == 0 ? null : source.StopBits;
        channel.Parity = source.Parity;
    }

    private static void MarkIncomplete(DashboardChannel channel)
    {
        channel.TrafficIncomplete = true;
        channel.TrafficGaps++;
    }

    private static double Delta(uint current, double? previous) =>
        unchecked(current - (uint)(previous ?? 0));
}
