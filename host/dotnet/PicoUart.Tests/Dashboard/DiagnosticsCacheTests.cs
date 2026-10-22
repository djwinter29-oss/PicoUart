using PicoUart.Dashboard;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class DiagnosticsCacheTests
{
    private static ChannelDiagnostics Diagnostics(int index) => new(0, 115200, 8, 1, "N",
        (byte)(index * 4), (byte)(index * 4 + 1), new());

    [Fact]
    public void RefreshReadsEveryChannelOnceThenOnlyChangedGenerations()
    {
        var cache = new DiagnosticsCache();
        var reads = new List<int>();
        ChannelDiagnostics Read(int index)
        {
            reads.Add(index);
            return Diagnostics(index);
        }
        byte[] generations = [1, 1, 1, 1, 1, 1];

        var first = cache.Refresh(generations, Read);
        Assert.Equal([0, 1, 2, 3, 4, 5], reads);
        Assert.Equal(Enumerable.Range(0, 6).Select(index => (byte?)(index * 4)), first.Select(item => item.TxPin));

        reads.Clear();
        cache.Refresh(generations, Read);
        Assert.Empty(reads);

        generations[2] = 2;
        cache.Refresh(generations, Read);
        Assert.Equal([2], reads);

        reads.Clear();
        generations[5] = 0; // A generation counter that wraps around is still a change.
        generations[0] = 255;
        cache.Refresh(generations, Read);
        Assert.Equal([0, 5], reads);
    }

    [Fact]
    public void FailedReadEmptiesTheCacheSoTheNextRefreshReadsEverything()
    {
        var cache = new DiagnosticsCache();
        var reads = new List<int>();
        var failing = false;
        ChannelDiagnostics Read(int index)
        {
            reads.Add(index);
            if (failing && index == 3) throw new IOException("channel 3 unavailable");
            return Diagnostics(index);
        }
        byte[] generations = [1, 1, 1, 1, 1, 1];
        cache.Refresh(generations, Read);

        reads.Clear();
        failing = true;
        generations[3] = 2;
        Assert.Throws<IOException>(() => cache.Refresh(generations, Read));
        Assert.Equal([3], reads);

        reads.Clear();
        failing = false;
        cache.Refresh(generations, Read);
        Assert.Equal([0, 1, 2, 3, 4, 5], reads);
    }
}
