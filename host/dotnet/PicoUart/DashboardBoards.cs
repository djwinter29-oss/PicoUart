using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace PicoUart;

public sealed record DashboardBoard(string Id, string ProductName, string? SerialNumber, string DevicePath);
public sealed record DashboardBoardList(DashboardBoard[] Boards, string? DefaultBoardId);

public sealed class DashboardBoards : IHostedService, IDisposable
{
    private readonly object gate = new();
    private readonly string? serialNumber;
    private readonly string? devicePath;
    private readonly Func<HidBoard[]> discover;
    private readonly Func<string?, string?, DashboardService> createMonitor;
    private readonly Dictionary<string, DashboardBoard> known = new();
    private readonly Dictionary<string, DashboardService> monitors = new();
    private bool closed;

    public DashboardBoards(string? serialNumber, string? devicePath)
        : this(serialNumber, devicePath, PicoUartHid.Discover, (serial, path) => new(serial, path)) { }

    internal DashboardBoards(string? serialNumber, string? devicePath, Func<HidBoard[]> discover,
        Func<string?, string?, DashboardService> createMonitor)
    {
        this.serialNumber = serialNumber;
        this.devicePath = devicePath;
        this.discover = discover;
        this.createMonitor = createMonitor;
    }

    public DashboardBoardList Boards()
    {
        lock (gate)
        {
            if (closed) throw new InvalidOperationException("dashboard is shutting down");
            var seen = new HashSet<string>();
            var boards = discover().Select(device =>
            {
                var serial = string.IsNullOrEmpty(device.SerialNumber) ? null : device.SerialNumber;
                var identity = serial is null ? $"path:{device.DevicePath}" : $"serial:{serial}";
                var id = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(identity)));
                if (!seen.Add(id))
                    throw new InvalidOperationException("duplicate PicoUart board identity; board serials must be unique");
                var board = new DashboardBoard(id, string.IsNullOrEmpty(device.ProductName) ? "PicoUart" : device.ProductName,
                    serial, device.DevicePath);
                return board;
            }).ToArray();
            foreach (var board in boards) known[board.Id] = board;
            var preferred = boards.FirstOrDefault(board =>
                (serialNumber is not null && board.SerialNumber == serialNumber)
                || (devicePath is not null && board.DevicePath == devicePath));
            return new(boards, preferred?.Id ??
                (serialNumber is null && devicePath is null ? boards.FirstOrDefault()?.Id : null));
        }
    }

    internal DashboardService Reader(string? boardId)
    {
        lock (gate)
        {
            if (closed) throw new InvalidOperationException("dashboard is shutting down");
            if (boardId is null || !known.TryGetValue(boardId, out var board))
                throw new InvalidOperationException("unknown board; rescan and select a listed board");
            if (!monitors.TryGetValue(boardId, out var monitor))
            {
                monitor = createMonitor(board.SerialNumber, board.SerialNumber is null ? board.DevicePath : null);
                try { monitor.StartAsync(CancellationToken.None).GetAwaiter().GetResult(); }
                catch
                {
                    monitor.Dispose();
                    throw;
                }
                monitors.Add(boardId, monitor);
            }
            return monitor;
        }
    }

    public string SnapshotJson(string? boardId) => boardId is null
        ? JsonSerializer.Serialize(new DashboardSnapshot(), Protocol.JsonOptions) : Reader(boardId).SnapshotJson();

    public void Action(string? boardId, string action) => Reader(boardId).Action(action);

    public void SaveConfiguration(string? boardId, uint profile, bool unsafeOverclock) =>
        Reader(boardId).SaveConfiguration(profile, unsafeOverclock);

    public Task StartAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    public async Task StopAsync(CancellationToken cancellationToken)
    {
        DashboardService[] readers;
        lock (gate)
        {
            closed = true;
            readers = monitors.Values.ToArray();
        }
        await Task.WhenAll(readers.Select(reader => reader.StopAsync(cancellationToken)));
    }

    public void Dispose()
    {
        lock (gate)
        {
            closed = true;
            foreach (var reader in monitors.Values) reader.Dispose();
        }
    }
}
