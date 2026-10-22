using PicoUart.Wire;

namespace PicoUart.Dashboard;

/// <summary>Caches per-channel diagnostics and re-reads only channels whose firmware generation changed.</summary>
internal sealed class DiagnosticsCache
{
    private readonly ChannelDiagnostics?[] items = new ChannelDiagnostics?[Protocol.ChannelCount];
    private readonly byte?[] generations = new byte?[Protocol.ChannelCount];

    /// <summary>
    /// Returns all channels, calling <paramref name="read"/> only for stale ones. A failed read empties the cache so
    /// the next refresh re-reads everything.
    /// </summary>
    public ChannelDiagnostics[] Refresh(byte[] current, Func<int, ChannelDiagnostics> read)
    {
        try
        {
            for (var index = 0; index < items.Length; index++)
            {
                if (items[index] is not null && generations[index] == current[index]) continue;
                items[index] = read(index);
                // The generation from before the read is kept: a newer change only costs one extra read.
                generations[index] = current[index];
            }
        }
        catch
        {
            Array.Clear(items);
            Array.Clear(generations);
            throw;
        }
        return items.Select(item => item!).ToArray();
    }
}
