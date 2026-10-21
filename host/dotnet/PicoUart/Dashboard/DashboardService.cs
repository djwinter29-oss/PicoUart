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

    private void ReadConfigurationSave(PicoUartHid connection)
    {
        lock (gate)
        {
            try
            {
                snapshot.ConfigurationSave = connection.ReadConfigurationSave();
                snapshot.ConfigurationSaveError = null;
            }
            catch (Exception error) when (HidErrors.IsDeviceError(error))
            {
                snapshot.ConfigurationSave = null;
                snapshot.ConfigurationSaveError = error.Message;
            }
        }
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
                    ReadLed(connection);
                    ReadConfigurationSave(connection);
                    try
                    {
                        var diagnostics = connection.ReadChannelDiagnostics();
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
                    try
                    {
                        var identity = connection.ReadIdentity();
                        lock (gate)
                        {
                            snapshot.Identity = identity;
                            snapshot.IdentityError = null;
                        }
                    }
                    catch (Exception error) when (HidErrors.IsDeviceError(error))
                    {
                        lock (gate)
                        {
                            snapshot.Identity = null;
                            snapshot.IdentityError = error.Message;
                        }
                    }
                    try
                    {
                        var runtime = connection.ReadRuntime();
                        lock (gate)
                        {
                            snapshot.SetRuntimeReadResult(runtime, null);
                        }
                    }
                    catch (Exception error) when (HidErrors.IsDeviceError(error))
                    {
                        lock (gate)
                        {
                            snapshot.SetRuntimeReadResult(null, error.Message);
                        }
                    }
                    try
                    {
                        var configuration = connection.ReadConfiguration();
                        lock (gate)
                        {
                            snapshot.Configuration = configuration;
                            snapshot.ConfigurationError = null;
                            snapshot.ConfigurationSave = null;
                            snapshot.ConfigurationSaveError = null;
                        }
                    }
                    catch (Exception error) when (HidErrors.IsDeviceError(error))
                    {
                        lock (gate)
                        {
                            snapshot.Configuration = null;
                            snapshot.ConfigurationError = error.Message;
                        }
                    }
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
                        catch (Exception error) when (HidErrors.IsDeviceError(error))
                        {
                            lock (gate)
                            {
                                snapshot.Hardware = null;
                                snapshot.HardwareError = error.Message;
                            }
                        }
                    }
                    catch (Exception error) when (HidErrors.IsDeviceError(error))
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