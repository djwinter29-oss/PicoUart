// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Reflection;
using System.Text.Json;
using PicoUart.Dashboard;
using PicoUart.Hid;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class DashboardBoardsTests
{
    [Fact]
    public void DiscoveryContinuesAfterDeviceSpecificReadFailure()
    {
        var devices = HidDiscovery.DiscoverDevices(
            new[] { "healthy", "unreadable" },
            device => device == "unreadable"
                ? throw new IOException("identity report failed")
                : new HidBoard($"/{device}", device, "PicoUart"),
            (device, error) => new($"/{device}", device, "PicoUart unavailable", error.Message));

        Assert.Equal(2, devices.Length);
        Assert.Equal("healthy", devices[0].SerialNumber);
        Assert.Null(devices[0].Error);
        Assert.Equal("unreadable", devices[1].SerialNumber);
        Assert.Equal("identity report failed", devices[1].Error);
    }

    [Fact]
    public async Task ConcurrentViewersShareOneReaderPerBoardUntilShutdown()
    {
        var created = new ConcurrentBag<(string? Serial, string? Path, DashboardService Reader)>();
        using var boards = new DashboardBoards(null, null,
            () => [new("/a", "test-A", "PicoUart"), new("/b", "test-B", "PicoUart")],
            (serial, path) =>
            {
                var reader = new DashboardService(serial, path);
                created.Add((serial, path, reader));
                return reader;
            });
        var listing = boards.Boards();
        Assert.Empty(created);
        var first = listing.Boards[0].Id;
        var second = listing.Boards[1].Id;
        Assert.Equal(first, listing.DefaultBoardId);
        var readers = await Task.WhenAll(Enumerable.Range(0, 60)
            .Select(index => Task.Run(() => boards.Reader(index % 2 == 0 ? first : second))));
        Assert.Equal(2, created.Count);
        Assert.All(created, monitor => Assert.Null(monitor.Path));
        for (var index = 0; index < readers.Length; index++)
            Assert.Same(readers[index % 2], readers[index]);
        Assert.NotSame(readers[0], readers[1]);
        using var empty = JsonDocument.Parse(boards.SnapshotJson(null));
        Assert.False(empty.RootElement.GetProperty("connected").GetBoolean());
        Assert.Throws<InvalidOperationException>(() => boards.Reader("unknown"));
        Assert.Throws<InvalidOperationException>(() => boards.Action(null, "toggle-led"));
        Assert.Throws<InvalidOperationException>(() => boards.SaveConfiguration(null, 0, false));
        Assert.Throws<InvalidOperationException>(() => boards.SaveConfiguration("unknown", 0, false));
        await boards.StopAsync(CancellationToken.None);
        Assert.Throws<InvalidOperationException>(() => boards.Reader(first));
    }

    [Fact]
    public async Task RescanKeepsMissingReadersAndReconnectsBySerialRatherThanOldPath()
    {
        HidBoard[] attached = [new("/a", "test-A", "PicoUart"), new("/b", "test-B", "PicoUart")];
        using var boards = new DashboardBoards("test-A", null, () => attached,
            (serial, path) => new(serial, path));
        var first = boards.Boards().DefaultBoardId!;
        var reader = boards.Reader(first);
        attached = [new("/b", "test-B", "PicoUart")];
        Assert.Null(boards.Boards().DefaultBoardId);
        Assert.Same(reader, boards.Reader(first));
        attached = [new("/new-path", "test-A", "PicoUart")];
        Assert.Equal(first, boards.Boards().DefaultBoardId);
        Assert.Same(reader, boards.Reader(first));
        await boards.StopAsync(CancellationToken.None);
    }

    [Fact]
    public async Task RepeatedMissingScansRetireReaderBeforeItCanBeRecreated()
    {
        HidBoard[] attached = [new("/a", "test-A", "PicoUart")];
        var created = new List<DashboardService>();
        using var boards = new DashboardBoards(null, null, () => attached, (serial, path) =>
        {
            var reader = new DashboardService(serial, path);
            created.Add(reader);
            return reader;
        });
        var id = boards.Boards().Boards[0].Id;
        var original = boards.Reader(id);
        attached = [];

        boards.Boards();
        boards.Boards();
        boards.Boards();
        await WaitForRetirement();

        Assert.Throws<InvalidOperationException>(() => boards.Reader(id));
        Assert.Equal(0, boards.RetiringReaderCount);
        attached = [new("/new-path", "test-A", "PicoUart")];
        boards.Boards();
        Assert.NotSame(original, boards.Reader(id));
        Assert.Equal(2, created.Count);
        await boards.StopAsync(CancellationToken.None);

        async Task WaitForRetirement()
        {
            var deadline = Stopwatch.StartNew();
            while (boards.RetiringReaderCount != 0 && deadline.Elapsed < TimeSpan.FromSeconds(2))
                await Task.Delay(10);
        }
    }

    [Fact]
    public async Task ReaderReturnsPromptlyWhileRetiredReaderCleanupIsPending()
    {
        using var boards = new DashboardBoards(null, null,
            () => [new("/a", "test-A", "PicoUart")],
            (serial, path) => new(serial, path));
        var id = boards.Boards().Boards[0].Id;
        var cleanup = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var retiring = (Dictionary<string, Task>)typeof(DashboardBoards)
            .GetField("retiring", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(boards)!;
        retiring.Add(id, cleanup.Task);

        var error = Assert.Throws<IOException>(() => boards.Reader(id));
        Assert.Contains("cleanup is still pending", error.Message);

        cleanup.SetResult();
        Assert.NotNull(boards.Reader(id));
        await boards.StopAsync(CancellationToken.None);
    }

    [Fact]
    public async Task PathFallbackAndDuplicateIdentitiesAreExplicit()
    {
        HidBoard[] attached = [new("/a", null, "PicoUart"), new("/b", null, "")];
        string? selectedPath = null;
        using var boards = new DashboardBoards(null, "/b", () => attached, (serial, path) =>
        {
            selectedPath = path;
            Assert.Null(serial);
            // Do not try to open an actual OS device in this test.
            return new("nonexistent-test-board", null);
        });
        var listing = boards.Boards();
        Assert.NotEqual(listing.Boards[0].Id, listing.Boards[1].Id);
        Assert.Equal(listing.Boards[1].Id, listing.DefaultBoardId);
        Assert.Equal("PicoUart", listing.Boards[1].ProductName);
        boards.Reader(listing.DefaultBoardId);
        Assert.Equal("/b", selectedPath);
        attached = [new("/a", "duplicate", "PicoUart"), new("/b", "duplicate", "PicoUart")];
        Assert.Throws<InvalidOperationException>(() => boards.Boards());
        await boards.StopAsync(CancellationToken.None);
    }

    [Fact]
    public async Task DiscoveryFailuresAreReportedWithoutHidingHealthyBoardsOrRetiringKnownReaders()
    {
        HidBoard[] attached = [new("/board", "test-A", "PicoUart")];
        using var boards = new DashboardBoards(null, null, () => attached, (serial, path) => new(serial, path));
        var initial = boards.Boards();
        var boardId = initial.Boards[0].Id;
        var reader = boards.Reader(boardId);

        attached =
        [
            new("/board", "test-A", "PicoUart", "identity report failed"),
            new("/healthy", "test-B", "PicoUart")
        ];
        var listing = boards.Boards();

        var healthy = Assert.Single(listing.Boards);
        Assert.Equal("test-B", healthy.SerialNumber);
        Assert.Equal(healthy.Id, listing.DefaultBoardId);
        var error = Assert.Single(listing.DiscoveryErrors);
        Assert.Equal("/board", error.DevicePath);
        Assert.Equal("test-A", error.SerialNumber);
        Assert.Equal("identity report failed", error.Error);
        Assert.Same(reader, boards.Reader(boardId));

        await boards.StopAsync(CancellationToken.None);
    }
}
