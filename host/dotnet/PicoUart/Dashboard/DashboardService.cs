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

    public void Action(string action)
    {
        lock (gate)
        {
            var connection = client
                ?? throw new InvalidOperationException(snapshot.Error ?? "PicoUart HID interface is not connected");
            if (action == "toggle-led")
            {
                try
                {
                    connection.ToggleLed();
                    ReadTelemetry(connection);
                }
                catch
                {
                    snapshot.Led = null;
                    snapshot.LedError = "LED operation unconfirmed; reread telemetry";
                    throw;
                }
            }
            else if (action == "reset") connection.ResetBoard();
            else throw new ArgumentException("unknown board action", nameof(action));
        }
    }

    public void SaveConfiguration(uint profile, bool unsafeOverclock)
    {
        lock (gate)
        {
            var connection = client
                ?? throw new InvalidOperationException(snapshot.Error ?? "PicoUart HID interface is not connected");
            try
            {
                snapshot.ConfigurationSave = connection.SaveConfiguration(profile, unsafeOverclock);
                snapshot.ConfigurationSaveError = null;
            }
            catch (Exception error) when (HidErrors.IsDeviceError(error) || error is ArgumentException)
            {
                snapshot.ConfigurationSave = null;
                snapshot.ConfigurationSaveError = error.Message;
                throw;
            }
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
            var control = connection.ReadControl();
            snapshot.Configuration = control.Configuration;
            snapshot.ConfigurationError = null;
            snapshot.ConfigurationSave = control.Command == 5 ? new(control.Status, control.RequestedProfile) : null;
            snapshot.ConfigurationSaveError = null;
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
                    // Keep board operations and stream reads under the same lock order.
                    lock (gate)
                    {
                        if (Stopwatch.GetTimestamp() >= nextMetadataRead)
                        {
                            if (ReadStatic(connection))
                            {
                                ReadTelemetry(connection);
                                if (!initialChannelsRead)
                                {
                                    var failed = false;
                                    for (var index = 0; index < Protocol.ChannelCount; index++)
                                    {
                                        try { snapshot.ApplyFeature(connection.ReadChannel(index)); }
                                        catch (Exception error) when (HidErrors.IsDeviceError(error))
                                        {
                                            failed = true;
                                            snapshot.DiagnosticsError = error.Message;
                                            snapshot.ChannelDiagnostics[index] = null;
                                        }
                                    }
                                    initialChannelsRead = !failed;
                                    if (!failed) snapshot.DiagnosticsError = null;
                                }
                            }
                            nextMetadataRead = Stopwatch.GetTimestamp() + Stopwatch.Frequency;
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
