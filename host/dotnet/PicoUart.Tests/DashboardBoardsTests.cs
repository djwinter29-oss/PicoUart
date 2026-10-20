using System.Collections.Concurrent;
using System.Text.Json;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class DashboardBoardsTests
{
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
}
