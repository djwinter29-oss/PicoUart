using System.Text.Json;

namespace PicoUart;

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
    public string Backend { get; set; } = "unknown";
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
    public DateTimeOffset? UpdatedAt { get; set; }
    public byte? Sequence { get; set; }
    public bool TrafficIncomplete { get; set; }
    public BoardStatus? Board { get; set; }
    public uint?[] OverflowCounts { get; set; } = new uint?[Protocol.ChannelCount];
    public DashboardChannel[] Channels { get; } = Enumerable.Range(0, Protocol.ChannelCount)
        .Select(index => new DashboardChannel(index)).ToArray();

    public void Apply(Status status)
    {
        Connected = true;
        Error = null;
        if (Sequence is not null && status.Sequence != (Sequence + 1) % 256) TrafficIncomplete = true;
        Sequence = status.Sequence;
        UpdatedAt = DateTimeOffset.UtcNow;
        foreach (var source in status.Channels)
        {
            var channel = Channels[source.Id];
            channel.Health = source.Health;
            channel.HealthLabels = Protocol.DecodeHealth(source.Health);
            channel.State = (source.Health & 0xC6) != 0 ? "attention"
                : (source.Health & 1) != 0 ? "ready" : "initializing";
            channel.Backend = (source.Health & 0x20) != 0 ? "PIO" : "Hardware";
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
                TrafficIncomplete = true;
        }
    }
}

public sealed class DashboardService(string? serialNumber, string? devicePath) : BackgroundService
{
    private readonly object gate = new();
    private DashboardSnapshot snapshot = new();
    private PicoUartHid? client;

    public string SnapshotJson()
    {
        lock (gate) return JsonSerializer.Serialize(snapshot, Protocol.JsonOptions);
    }

    public void Action(string action)
    {
        lock (gate)
        {
            if (client is null)
                throw new InvalidOperationException(snapshot.Error ?? "PicoUart HID interface is not connected");
            if (action == "toggle-led") client.ToggleLed();
            else client.ResetBoard();
        }
    }

    protected override Task ExecuteAsync(CancellationToken stoppingToken) => Task.Run(async () =>
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            PicoUartHid? connection = null;
            try
            {
                connection = new(serialNumber, devicePath);
                lock (gate)
                {
                    client = connection;
                    snapshot = new() { Connected = true };
                }
                var nextMetadataRead = DateTimeOffset.MinValue;
                while (!stoppingToken.IsCancellationRequested)
                {
                    var status = connection.ReadStatus();
                    if (status is not null)
                    {
                        lock (gate) snapshot.Apply(status);
                    }
                    if (DateTimeOffset.UtcNow < nextMetadataRead) continue;
                    try
                    {
                        var board = connection.ReadBoardStatus();
                        var counts = connection.ReadOverflowCounts();
                        lock (gate)
                        {
                            snapshot.Board = board;
                            snapshot.OverflowCounts = counts.Select(count => (uint?)count).ToArray();
                            snapshot.MetadataError = null;
                        }
                        try
                        {
                            var hardware = connection.ReadHardwareInfo();
                            lock (gate)
                            {
                                snapshot.Hardware = hardware;
                                snapshot.HardwareError = null;
                            }
                        }
                        catch (Exception error) when (IsDeviceError(error))
                        {
                            lock (gate)
                            {
                                snapshot.Hardware = null;
                                snapshot.HardwareError = error.Message;
                            }
                        }
                    }
                    catch (Exception error) when (IsDeviceError(error))
                    {
                        lock (gate)
                        {
                            snapshot.Board = null;
                            snapshot.OverflowCounts = new uint?[Protocol.ChannelCount];
                            snapshot.MetadataError = error.Message;
                            snapshot.Hardware = null;
                            snapshot.HardwareError = null;
                        }
                    }
                    nextMetadataRead = DateTimeOffset.UtcNow.AddSeconds(2);
                }
            }
            catch (Exception error) when (IsDeviceError(error))
            {
                lock (gate)
                {
                    snapshot.Connected = false;
                    snapshot.Error = error.Message;
                }
            }
            finally
            {
                lock (gate) client = null;
                try { connection?.Dispose(); }
                catch (IOException) { }
            }
            try { await Task.Delay(1000, stoppingToken); }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested) { break; }
        }
    }, stoppingToken);

    public static bool IsDeviceError(Exception error) => error is IOException or InvalidOperationException
        or UnauthorizedAccessException or NotSupportedException or TimeoutException;
}