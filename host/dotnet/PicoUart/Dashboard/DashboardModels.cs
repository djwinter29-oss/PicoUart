using System.Diagnostics;
using PicoUart.Wire;

namespace PicoUart.Dashboard;

public sealed class Traffic
{
    public long UartTx { get; set; }
    public long UartRx { get; set; }
    public long UsbTx { get; set; }
    public long UsbRx { get; set; }
}

public sealed class DashboardChannel(int id)
{
    public int Id { get; } = id;
    public byte? Health { get; set; }
    public string[] HealthLabels { get; set; } = [];
    public string State { get; set; } = "unknown";
    public bool CdcOpen { get; set; }
    public int RingHighWatermark { get; set; }
    public Traffic Traffic { get; set; } = new();
    public Traffic Totals { get; } = new();
    public DateTimeOffset? UpdatedAt { get; set; }
    public ushort? Sequence { get; set; }
    public ulong ObservedIntervalMs { get; set; }
    public long TrafficGaps { get; set; }
    public bool TrafficIncomplete { get; set; }
    internal long LastInputTimestamp { get; set; }
    internal bool Stale { get; set; }
}

public sealed class DashboardSnapshot
{
    public bool Connected { get; set; }
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
    public uint?[] OverflowCounts { get; } = new uint?[Protocol.ChannelCount];
    public DashboardChannel[] Channels { get; } = Enumerable.Range(0, Protocol.ChannelCount)
        .Select(index => new DashboardChannel(index)).ToArray();

    internal void SetRuntimeReadResult(RuntimeStatus? runtime, string? error)
    {
        Runtime = runtime;
        RuntimeError = error;
    }

    /// <summary>Refresh on-demand state without advancing interrupt traffic or freshness.</summary>
    public void ApplyFeature(ChannelStatus source) => ApplyState(source);

    public void Apply(ChannelStatus source)
    {
        var channel = Channels[source.Id];
        var now = Stopwatch.GetTimestamp();
        var first = channel.Sequence is null;
        var advance = first ? 1 : (source.Sequence - channel.Sequence!.Value + 65536) & 65535;
        if (!first && (advance == 0 || advance >= 32768))
        {
            MarkIncomplete(channel);
            return;
        }
        var gap = !first && (source.Sequence != (ushort)((channel.Sequence!.Value + 1) & 65535)
            || Stopwatch.GetElapsedTime(channel.LastInputTimestamp, now).TotalSeconds >= 3);
        var exact = source.IntervalMs is > 0 and < uint.MaxValue
            && source.ControllerTxBytes != uint.MaxValue && source.ControllerRxBytes != uint.MaxValue
            && source.CdcTxBytes != uint.MaxValue && source.CdcRxBytes != uint.MaxValue;
        if (!first && ((gap && !channel.Stale) || !exact))
            MarkIncomplete(channel);
        Connected = true;
        Error = null;
        ApplyState(source);
        channel.Sequence = source.Sequence;
        channel.UpdatedAt = UpdatedAt = DateTimeOffset.UtcNow;
        channel.LastInputTimestamp = now;
        channel.Stale = false;
        channel.Traffic = first ? new() : new()
        {
            UartTx = source.ControllerTxBytes, UartRx = source.ControllerRxBytes,
            UsbTx = source.CdcTxBytes, UsbRx = source.CdcRxBytes
        };
        if (first) return;
        channel.Totals.UartTx = Add(channel.Totals.UartTx, source.ControllerTxBytes);
        channel.Totals.UartRx = Add(channel.Totals.UartRx, source.ControllerRxBytes);
        channel.Totals.UsbTx = Add(channel.Totals.UsbTx, source.CdcTxBytes);
        channel.Totals.UsbRx = Add(channel.Totals.UsbRx, source.CdcRxBytes);
        if (exact) channel.ObservedIntervalMs += source.IntervalMs;
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
            channel.Health = null;
            channel.HealthLabels = [];
            channel.State = "stale";
            channel.Traffic = new();
        }
    }

    private void ApplyState(ChannelStatus source)
    {
        var channel = Channels[source.Id];
        channel.Health = source.Health;
        channel.HealthLabels = Protocol.DecodeHealth(source.Health);
        channel.State = (source.Health & 0xC6) != 0 ? "attention"
            : (source.Health & 1) != 0 ? "ready" : "initializing";
        channel.CdcOpen = (source.Health & 0x10) != 0;
        channel.RingHighWatermark = source.RingHighWatermark;
        ChannelDiagnostics[source.Id] = source.Diagnostics;
        OverflowCounts[source.Id] = source.Diagnostics.Faults["rx_overrun"].Count;
    }

    private static void MarkIncomplete(DashboardChannel channel)
    {
        channel.TrafficIncomplete = true;
        channel.TrafficGaps++;
    }

    private static long Add(long total, uint delta) => total > long.MaxValue - delta ? long.MaxValue : total + delta;
}
