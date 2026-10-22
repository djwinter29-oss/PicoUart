using System.Text.Json;
using PicoUart.Hid;
using PicoUart.Wire;

namespace PicoUart.Dashboard;

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
            if (action == "toggle-led")
            {
                client.ToggleLed();
                ReadLed(client);
            }
            else client.ResetBoard();
        }
    }

    public void SaveConfiguration(uint profile, bool unsafeOverclock)
    {
        lock (gate)
        {
            if (client is null)
                throw new InvalidOperationException(snapshot.Error ?? "PicoUart HID interface is not connected");
            try
            {
                snapshot.ConfigurationSave = client.SaveConfiguration(profile, unsafeOverclock);
                snapshot.ConfigurationSaveError = null;
            }
            catch (Exception error) when (HidErrors.IsDeviceError(error) || error is ArgumentException)
            {
                snapshot.ConfigurationSaveError = error.Message;
                throw;
            }
        }
    }

    /// <summary>State read from one HID connection; static reports are fetched once and diagnostics cached by generation.</summary>
    private sealed class ConnectionCache
    {
        public readonly HashSet<string> StaticLoaded = [];
        public BoardStatus? Board;
        public string? BoardError;
        public string? TelemetryError;
        public double? Temperature;
        public readonly DiagnosticsCache Diagnostics = new();
    }

    /// <summary>Combine static board info with the latest temperature; callers hold the gate.</summary>
    private void PublishBoard(ConnectionCache cache)
    {
        snapshot.MetadataError = cache.TelemetryError ?? cache.BoardError;
        snapshot.Board = cache.TelemetryError is null && cache.Temperature is double temperature && cache.Board is not null
            ? cache.Board with { TemperatureCelsius = temperature }
            : null;
    }

    private byte[]? ReadTelemetry(PicoUartHid connection, ConnectionCache cache)
    {
        try
        {
            var telemetry = connection.ReadTelemetry();
            lock (gate)
            {
                cache.TelemetryError = null;
                cache.Temperature = telemetry.TemperatureCelsius;
                snapshot.Led = telemetry.Led;
                snapshot.LedError = null;
                snapshot.SetRuntimeReadResult(telemetry.Runtime, null);
                snapshot.ConfigurationSave = telemetry.ConfigurationSave;
                snapshot.ConfigurationSaveError = null;
                snapshot.OverflowCounts = telemetry.OverflowCounts.Select(count => (uint?)count).ToArray();
                PublishBoard(cache);
            }
            return telemetry.DiagnosticsGeneration;
        }
        catch (Exception error) when (HidErrors.IsDeviceError(error))
        {
            lock (gate)
            {
                cache.TelemetryError = error.Message;
                snapshot.Led = null;
                snapshot.LedError = error.Message;
                snapshot.SetRuntimeReadResult(null, error.Message);
                snapshot.ConfigurationSave = null;
                snapshot.ConfigurationSaveError = error.Message;
                snapshot.OverflowCounts = new uint?[Protocol.ChannelCount];
                PublishBoard(cache);
            }
            return null;
        }
    }

    private void ReadDiagnostics(PicoUartHid connection, ConnectionCache cache, byte[] generations)
    {
        try
        {
            var diagnostics = cache.Diagnostics.Refresh(generations, connection.ReadChannelDiagnostic);
            lock (gate)
            {
                snapshot.ChannelDiagnostics = diagnostics;
                snapshot.DiagnosticsError = null;
            }
        }
        catch (Exception error) when (HidErrors.IsDeviceError(error))
        {
            lock (gate)
            {
                snapshot.ChannelDiagnostics = null;
                snapshot.DiagnosticsError = error.Message;
            }
        }
    }

    /// <summary>Read a report that cannot change while connected; failures are retried until one read succeeds.</summary>
    private void LoadOnce<T>(ConnectionCache cache, string name, Func<T> read, Action<T?, string?> publish)
        where T : class
    {
        if (cache.StaticLoaded.Contains(name)) return;
        try
        {
            var value = read();
            cache.StaticLoaded.Add(name);
            lock (gate) publish(value, null);
        }
        catch (Exception error) when (HidErrors.IsDeviceError(error))
        {
            lock (gate) publish(null, error.Message);
        }
    }

    private void ReadStatic(PicoUartHid connection, ConnectionCache cache)
    {
        LoadOnce(cache, "identity", connection.ReadIdentity, (value, error) =>
        {
            snapshot.Identity = value;
            snapshot.IdentityError = error;
        });
        LoadOnce(cache, "configuration", connection.ReadConfiguration, (value, error) =>
        {
            snapshot.Configuration = value;
            snapshot.ConfigurationError = error;
        });
        LoadOnce(cache, "hardware", connection.ReadHardwareInfo, (value, error) =>
        {
            snapshot.Hardware = value;
            snapshot.HardwareError = error;
        });
        LoadOnce(cache, "board", connection.ReadBoardStatus, (value, error) =>
        {
            cache.Board = value;
            cache.BoardError = error;
            PublishBoard(cache);
        });
    }

    private void ReadLed(PicoUartHid connection)
    {
        try
        {
            var led = connection.ReadLed();
            lock (gate)
            {
                snapshot.Led = led;
                snapshot.LedError = null;
            }
        }
        catch (Exception error) when (HidErrors.IsDeviceError(error))
        {
            lock (gate)
            {
                snapshot.Led = null;
                snapshot.LedError = error.Message;
            }
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
                var cache = new ConnectionCache();
                var nextMetadataRead = DateTimeOffset.MinValue;
                while (!stoppingToken.IsCancellationRequested)
                {
                    var status = connection.ReadStatus();
                    if (status is not null)
                    {
                        lock (gate) snapshot.Apply(status);
                    }
                    var now = DateTimeOffset.UtcNow;
                    if (now < nextMetadataRead) continue;
                    var generations = ReadTelemetry(connection, cache);
                    if (generations is not null) ReadDiagnostics(connection, cache, generations);
                    ReadStatic(connection, cache);
                    nextMetadataRead = now.AddSeconds(1);
                }
            }
            catch (Exception error) when (HidErrors.IsDeviceError(error))
            {
                lock (gate)
                {
                    snapshot.Connected = false;
                    snapshot.Error = error.Message;
                    snapshot.ChannelDiagnostics = null;
                    snapshot.Identity = null;
                    snapshot.Runtime = null;
                    snapshot.Board = null;
                    snapshot.Hardware = null;
                    snapshot.Configuration = null;
                    snapshot.ConfigurationError = null;
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
}