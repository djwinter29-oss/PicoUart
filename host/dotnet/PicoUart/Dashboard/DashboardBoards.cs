// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using PicoUart.Hid;
using PicoUart.Wire;

namespace PicoUart.Dashboard;

public sealed record DashboardBoard(string Id, string ProductName, string? SerialNumber, string DevicePath);
public sealed record DashboardBoardList(DashboardBoard[] Boards, string? DefaultBoardId);

public sealed class DashboardBoards : IHostedService, IDisposable, IDashboardBoards
{
    private const int MissingScansBeforeRetirement = 3;
    private readonly object gate = new();
    private readonly object scanGate = new();
    private readonly string? serialNumber;
    private readonly string? devicePath;
    private readonly Func<HidBoard[]> discover;
    private readonly Func<string?, string?, DashboardService> createMonitor;
    private readonly Dictionary<string, DashboardBoard> known = new();
    private readonly Dictionary<string, DashboardService> monitors = new();
    private readonly Dictionary<string, int> missingScans = new();
    private readonly Dictionary<string, Task> retiring = new();
    private bool closed;

    internal int RetiringReaderCount
    {
        get { lock (gate) return retiring.Count; }
    }

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
        lock (scanGate)
        {
            lock (gate)
                if (closed) throw new InvalidOperationException("dashboard is shutting down");

            var devices = discover();
            lock (gate)
            {
                if (closed) throw new InvalidOperationException("dashboard is shutting down");
                var seen = new HashSet<string>();
                var boards = devices.Select(device =>
                {
                    var serial = string.IsNullOrEmpty(device.SerialNumber) ? null : device.SerialNumber;
                    var identity = serial is null ? $"path:{device.DevicePath}" : $"serial:{serial}";
                    var id = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(identity)));
                    if (!seen.Add(id))
                        throw new InvalidOperationException(
                            "duplicate PicoUart board identity; board serials must be unique");
                    var board = new DashboardBoard(id,
                        string.IsNullOrEmpty(device.ProductName) ? "PicoUart" : device.ProductName, serial,
                        device.DevicePath);
                    return board;
                }).ToArray();
                foreach (var board in boards)
                {
                    known[board.Id] = board;
                    missingScans.Remove(board.Id);
                }
                foreach (var id in known.Keys.Where(id => !seen.Contains(id)).ToArray())
                {
                    var absentScans = missingScans.GetValueOrDefault(id) + 1;
                    if (absentScans < MissingScansBeforeRetirement)
                    {
                        missingScans[id] = absentScans;
                        continue;
                    }

                    known.Remove(id);
                    missingScans.Remove(id);
                    if (monitors.Remove(id, out var reader))
                    {
                        var retirement = Task.Run(async () =>
                        {
                            try { await reader.StopAsync(CancellationToken.None); }
                            finally { reader.Dispose(); }
                        });
                        retiring[id] = retirement;
                        _ = retirement.ContinueWith(completed =>
                        {
                            if (completed.IsFaulted) _ = completed.Exception;
                            if (!completed.IsCompletedSuccessfully) return;
                            lock (gate)
                            {
                                if (retiring.TryGetValue(id, out var current)
                                    && ReferenceEquals(current, retirement))
                                    retiring.Remove(id);
                            }
                        }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
                    }
                }
                var preferred = boards.FirstOrDefault(board =>
                    (serialNumber is not null && board.SerialNumber == serialNumber)
                    || (devicePath is not null && board.DevicePath == devicePath));
                return new(boards, preferred?.Id ??
                    (serialNumber is null && devicePath is null ? boards.FirstOrDefault()?.Id : null));
            }
        }
    }

    internal DashboardService Reader(string? boardId)
    {
        while (true)
        {
            lock (gate)
            {
                if (closed) throw new InvalidOperationException("dashboard is shutting down");
                if (boardId is null || !known.TryGetValue(boardId, out var board))
                    throw new InvalidOperationException("unknown board; rescan and select a listed board");
                if (retiring.TryGetValue(boardId, out var cleanup))
                {
                    if (!cleanup.IsCompleted)
                        throw new IOException("board cleanup is still pending; retry the request shortly");
                    cleanup.GetAwaiter().GetResult();
                    if (retiring.TryGetValue(boardId, out var current) && ReferenceEquals(current, cleanup))
                        retiring.Remove(boardId);
                    continue;
                }

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
    }

    public string SnapshotJson(string? boardId) => boardId is null
        ? JsonSerializer.Serialize(new DashboardSnapshot(), Protocol.JsonOptions) : Reader(boardId).SnapshotJson();

    public BoardControlStatus Action(string? boardId, string action) => Reader(boardId).Action(action);

    public BoardControlStatus SaveConfiguration(string? boardId, uint profile, bool unsafeOverclock) =>
        Reader(boardId).SaveConfiguration(profile, unsafeOverclock);

    public string DiagnosticsJson(string? boardId) => Reader(boardId).DiagnosticsJson();

    public Task StartAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    public async Task StopAsync(CancellationToken cancellationToken)
    {
        DashboardService[] readers;
        Task[] pendingRetirements;
        lock (gate)
        {
            closed = true;
            readers = monitors.Values.ToArray();
            pendingRetirements = retiring.Values.ToArray();
        }
        await Task.WhenAll(readers.Select(async reader =>
        {
            try { await reader.StopAsync(cancellationToken); }
            finally { reader.Dispose(); }
        }).Concat(pendingRetirements)).WaitAsync(cancellationToken);
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
