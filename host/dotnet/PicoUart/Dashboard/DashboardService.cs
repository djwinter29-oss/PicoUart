using System.Diagnostics;
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
        lock (gate)
        {
            snapshot.MarkStale();
            return JsonSerializer.Serialize(snapshot, Protocol.JsonOptions);
        }
    }

    public BoardControlStatus Action(string action)
    {
        lock (gate)
        {
            var connection = client
                ?? throw new InvalidOperationException(snapshot.Error ?? "PicoUart HID interface is not connected");
            try
            {
                var result = action switch
                {
                    "toggle-led" => connection.ToggleLed(),
                    "reset" => connection.ResetBoard(),
                    _ => throw new ArgumentException("unknown board action", nameof(action))
                };
                snapshot.ApplyAction(result);
                if (action == "toggle-led") ReadTelemetry(connection);
                return snapshot.Control!;
            }
            catch
            {
                if (connection.LastActionResult is not null)
                    snapshot.ApplyAction(connection.LastActionResult);
                if (action == "toggle-led")
                {
                    snapshot.Led = null;
                    snapshot.LedError = "LED operation unconfirmed; reread telemetry";
                }
                throw;
            }
        }
    }

    public BoardControlStatus SaveConfiguration(uint profile, bool unsafeOverclock)
    {
        lock (gate)
        {
            var connection = client
                ?? throw new InvalidOperationException(snapshot.Error ?? "PicoUart HID interface is not connected");
            try
            {
                snapshot.ConfigurationSave = connection.SaveConfiguration(profile, unsafeOverclock);
                snapshot.ConfigurationSaveError = null;
                if (connection.LastActionResult is not null) snapshot.ApplyAction(connection.LastActionResult);
                snapshot.ConfigurationSave = new(2, 0, profile);
                return snapshot.Control!;
            }
            catch (Exception error) when (HidErrors.IsDeviceError(error) || error is ArgumentException)
            {
                var result = connection.LastActionResult;
                if (result?.Action == 4)
                {
                    snapshot.ApplyAction(result);
                    snapshot.ConfigurationSave = new(result.Status, result.ErrorReason, profile);
                }
                snapshot.ConfigurationSaveError = error.Message;
                throw;
            }
        }
    }

    public string DiagnosticsJson()
    {
        lock (gate)
        {
            var connection = client
                ?? throw new InvalidOperationException(snapshot.Error ?? "PicoUart HID interface is not connected");
            var errors = new List<string>();
            for (var index = 0; index < Protocol.ChannelCount; index++)
            {
                try { snapshot.ChannelDiagnostics[index] = connection.ReadChannelDiagnostic(index); }
                catch (Exception error) when (HidErrors.IsDeviceError(error))
                {
                    snapshot.ChannelDiagnostics[index] = null;
                    errors.Add($"CDC{index}: {error.Message}");
                }
            }
            snapshot.DiagnosticsError = errors.Count == 0 ? null : string.Join("; ", errors);
            return JsonSerializer.Serialize(snapshot.ChannelDiagnostics, Protocol.JsonOptions);
        }
    }

    private void ReadTelemetry(PicoUartHid connection)
    {
        try
        {
            var info = snapshot.Identity ?? connection.ReadIdentity();
            var telemetry = connection.ReadTelemetry();
            snapshot.Led = telemetry.Led;
            snapshot.LedError = null;
            snapshot.SetRuntimeReadResult(telemetry.Runtime, null);
            snapshot.Hardware = new(info.Mcu, telemetry.SystemClockHz, info.McuId);
            snapshot.HardwareError = null;
            snapshot.Board = new(telemetry.TemperatureCelsius, info.FirmwareVersion,
                info.FirmwareMajor, info.FirmwareMinor, info.FirmwarePatch, (info.Capabilities & 2) != 0);
            snapshot.MetadataError = null;
        }
        catch (Exception error) when (HidErrors.IsDeviceError(error))
        {
            snapshot.Led = null;
            snapshot.LedError = error.Message;
            snapshot.SetRuntimeReadResult(null, error.Message);
            snapshot.Hardware = null;
            snapshot.HardwareError = error.Message;
            snapshot.Board = null;
            snapshot.MetadataError = error.Message;
        }
    }

    private bool ReadStatic(PicoUartHid connection)
    {
        try
        {
            snapshot.Identity ??= connection.ReadIdentity();
            snapshot.IdentityError = null;
        }
        catch (Exception error) when (HidErrors.IsDeviceError(error))
        {
            snapshot.IdentityError = error.Message;
            return false;
        }
        if (snapshot.Configuration is not null) return true;
        try
        {
            snapshot.Configuration = connection.ReadConfiguration();
            snapshot.ConfigurationError = null;
        }
        catch (Exception error) when (HidErrors.IsDeviceError(error))
        {
            snapshot.ConfigurationError = error.Message;
        }
        return true;
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
                var nextMetadataRead = Stopwatch.GetTimestamp();
                var initialChannelsRead = false;
                while (!stoppingToken.IsCancellationRequested)
                {
                    lock (gate)
                    {
                        if (Stopwatch.GetTimestamp() >= nextMetadataRead)
                        {
                            if (ReadStatic(connection))
                            {
                                ReadTelemetry(connection);
                                if (!initialChannelsRead && snapshot.Configuration is not null)
                                {
                                    var failed = false;
                                    for (var index = 0; index < Protocol.ChannelCount; index++)
                                    {
                                        try { snapshot.ApplyFeature(connection.ReadChannel(index)); }
                                        catch (Exception error) when (HidErrors.IsDeviceError(error))
                                        {
                                            failed = true;
                                            snapshot.DiagnosticsError = error.Message;
                                        }
                                    }
                                    initialChannelsRead = !failed;
                                }
                            }
                            nextMetadataRead = Stopwatch.GetTimestamp()
                                + Stopwatch.Frequency * snapshot.RefreshIntervalMs / 1000;
                        }
                        var status = connection.ReadStatus();
                        if (status is not null) snapshot.Apply(status);
                        snapshot.MarkStale();
                    }
                    await Task.Yield();
                }
            }
            catch (Exception error) when (HidErrors.IsDeviceError(error))
            {
                lock (gate) snapshot = new() { Error = error.Message };
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
