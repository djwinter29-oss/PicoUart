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
    public byte? Sequence { get; set; }
    public bool TrafficIncomplete { get; set; }
    public long TrafficGaps { get; set; }
    public ChannelDiagnostics[]? ChannelDiagnostics { get; set; }
    public string? DiagnosticsError { get; set; }
    public BoardStatus? Board { get; set; }
    public uint?[] OverflowCounts { get; set; } = new uint?[Protocol.ChannelCount];
    public DashboardChannel[] Channels { get; } = Enumerable.Range(0, Protocol.ChannelCount)
        .Select(index => new DashboardChannel(index)).ToArray();

    internal void SetRuntimeReadResult(RuntimeStatus? runtime, string? error)
    {
        Runtime = runtime;
        RuntimeError = error;
    }

    public void Apply(Status status)
    {
        Connected = true;
        Error = null;
        var incomplete = Sequence is not null && status.Sequence != (Sequence + 1) % 256;
        Sequence = status.Sequence;
        UpdatedAt = DateTimeOffset.UtcNow;
        foreach (var source in status.Channels)
        {
            var channel = Channels[source.Id];
            channel.Health = source.Health;
            channel.HealthLabels = Protocol.DecodeHealth(source.Health);
            channel.State = (source.Health & 0xC6) != 0 ? "attention"
                : (source.Health & 1) != 0 ? "ready" : "initializing";
            channel.CdcOpen = (source.Health & 0x10) != 0;
            channel.RingHighWatermark = source.RingHighWatermark;
            channel.Traffic = new()
            {
                UartTx = source.ControllerTxBytes, UartRx = source.ControllerRxBytes,
                UsbTx = source.CdcTxBytes, UsbRx = source.CdcRxBytes
            };
            channel.Totals.UartTx += source.ControllerTxBytes;
            channel.Totals.UartRx += source.ControllerRxBytes;
            channel.Totals.UsbTx += source.CdcTxBytes;
            channel.Totals.UsbRx += source.CdcRxBytes;
            if (source.ControllerTxBytes == ushort.MaxValue || source.ControllerRxBytes == ushort.MaxValue
                || source.CdcTxBytes == ushort.MaxValue || source.CdcRxBytes == ushort.MaxValue)
                incomplete = true;
        }
        if (incomplete)
        {
            TrafficIncomplete = true;
            TrafficGaps++;
        }
    }
}