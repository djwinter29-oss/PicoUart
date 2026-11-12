// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
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
    private string? unconfirmedConfigurationSaveError;

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
        var connection = Connection();
        try
        {
            var result = action switch
            {
                "toggle-led" => connection.ToggleLed(),
                "reset" => connection.ResetBoard(),
                _ => throw new ArgumentException("unknown board action", nameof(action))
            };
            lock (gate)
                if (ReferenceEquals(client, connection)) snapshot.ApplyAction(result);
            if (action == "toggle-led") ReadTelemetry(connection);
            return ToControlStatus(result);
        }
        catch (Exception error)
        {
            lock (gate)
            {
                if (ReferenceEquals(client, connection))
                {
                    if (connection.LastActionResult is not null)
                        snapshot.ApplyAction(connection.LastActionResult);
                    if (action == "toggle-led" && error is ControlOutcomeUnconfirmedException)
                    {
                        snapshot.Led = null;
                        snapshot.LedError = error.Message;
                    }
                    else if (action == "toggle-led" && connection.LastActionResult is null)
                        snapshot.LedError = error.Message;
                }
            }
            throw;
        }
    }

    public BoardControlStatus SaveConfiguration(uint profile, bool unsafeOverclock)
    {
        var connection = Connection();
        try
        {
            var result = connection.SaveConfiguration(profile, unsafeOverclock);
            lock (gate)
            {
                if (ReferenceEquals(client, connection))
                {
                    snapshot.ConfigurationSave = result;
                    snapshot.ConfigurationSaveError = null;
                    unconfirmedConfigurationSaveError = null;
                    if (connection.LastActionResult is not null) snapshot.ApplyAction(connection.LastActionResult);
                    snapshot.ConfigurationSave = new(2, 0, profile);
                }
            }
            return ToControlStatus(new(4, result.Status, result.ErrorReason));
        }
        catch (Exception error) when (HidErrors.IsDeviceError(error) || error is ArgumentException)
        {
            if (error is ControlOutcomeUnconfirmedException)
                RecordUnconfirmedConfigurationSaveError(error.Message);
            lock (gate)
            {
                if (ReferenceEquals(client, connection))
                {
                    var result = connection.LastActionResult;
                    if (result?.Action == 4)
                    {
                        snapshot.ApplyAction(result);
                        snapshot.ConfigurationSave = new(result.Status, result.ErrorReason, profile);
                    }
                    snapshot.ConfigurationSaveError = error.Message;
                }
            }
            throw;
        }
    }

    public string DiagnosticsJson()
    {
        var connection = Connection();
        var diagnostics = new ChannelDiagnostics?[Protocol.ChannelCount];
        var errors = new List<string>();
        for (var index = 0; index < Protocol.ChannelCount; index++)
        {
            try { diagnostics[index] = connection.ReadChannelDiagnostic(index); }
            catch (Exception error) when (HidErrors.IsDeviceError(error))
            {
                errors.Add($"CDC{index}: {error.Message}");
            }
        }
        return PublishDiagnostics(connection, diagnostics, errors);
    }

    internal string PublishDiagnostics(PicoUartHid connection, ChannelDiagnostics?[] diagnostics, List<string> errors)
    {
        lock (gate)
        {
            if (!ReferenceEquals(client, connection))
                throw new IOException("board connection changed during diagnostics; retry the request");
            Array.Copy(diagnostics, snapshot.ChannelDiagnostics, diagnostics.Length);
            snapshot.DiagnosticsError = errors.Count == 0 ? null : string.Join("; ", errors);
            return JsonSerializer.Serialize(diagnostics, Protocol.JsonOptions);
        }
    }

    private PicoUartHid Connection()
    {
        lock (gate)
            return client ?? throw new InvalidOperationException(
                snapshot.Error ?? "PicoUart HID interface is not connected");
    }

    private static BoardControlStatus ToControlStatus(ActionResult result) =>
        new(result.Action, result.Status, result.ErrorReason,
            result.Status is >= 3 and <= 6
                ? $"board action {Protocol.ActionStatusName(result.Status)} (error {result.ErrorReason})"
                : null);

    private void ReadTelemetry(PicoUartHid connection)
    {
        BoardIdentity? info = null;
        Telemetry? telemetry = null;
        string? errorMessage = null;
        try
        {
            info = connection.ReadIdentity();
            telemetry = connection.ReadTelemetry();
        }
        catch (Exception error) when (HidErrors.IsDeviceError(error))
        {
            errorMessage = error.Message;
        }
        lock (gate)
        {
            if (!ReferenceEquals(client, connection)) return;
            ApplyTelemetry(info, telemetry, errorMessage);
        }
    }

    private void ApplyTelemetry(BoardIdentity? info, Telemetry? telemetry, string? errorMessage)
    {
        if (errorMessage is not null)
        {
            snapshot.Led = null;
            snapshot.LedError = errorMessage;
            snapshot.SetRuntimeReadResult(null, errorMessage);
            snapshot.Hardware = null;
            snapshot.HardwareError = errorMessage;
            snapshot.Board = null;
            snapshot.MetadataError = errorMessage;
            return;
        }
        if (info is null || telemetry is null) return;
        snapshot.Led = telemetry.Led;
        snapshot.LedError = null;
        snapshot.SetRuntimeReadResult(telemetry.Runtime, null);
        snapshot.Hardware = new(info.Mcu, telemetry.SystemClockHz, info.McuId);
        snapshot.HardwareError = null;
        snapshot.Board = new(telemetry.TemperatureCelsius, info.FirmwareVersion,
            info.FirmwareMajor, info.FirmwareMinor, info.FirmwarePatch, (info.Capabilities & 2) != 0);
        snapshot.MetadataError = null;
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
                    ReplaceSnapshot(new() { Connected = true });
                }
                var nextMetadataRead = Stopwatch.GetTimestamp();
                var initialChannelsRead = false;
                while (!stoppingToken.IsCancellationRequested)
                {
                    bool metadataDue;
                    bool needsConfiguration;
                    lock (gate)
                    {
                        if (!ReferenceEquals(client, connection)) break;
                        metadataDue = Stopwatch.GetTimestamp() >= nextMetadataRead;
                        needsConfiguration = snapshot.Configuration is null;
                    }
                    if (metadataDue)
                    {
                        BoardIdentity? info = null;
                        ConfigurationStatus? configuration = null;
                        Telemetry? telemetry = null;
                        string? identityError = null;
                        string? configurationError = null;
                        string? telemetryError = null;
                        try { info = connection.ReadIdentity(); }
                        catch (Exception error) when (HidErrors.IsDeviceError(error))
                        {
                            identityError = error.Message;
                        }
                        if (info is not null)
                        {
                            if (needsConfiguration)
                            {
                                try { configuration = connection.ReadConfiguration(); }
                                catch (Exception error) when (HidErrors.IsDeviceError(error))
                                {
                                    configurationError = error.Message;
                                }
                            }
                            try { telemetry = connection.ReadTelemetry(); }
                            catch (Exception error) when (HidErrors.IsDeviceError(error))
                            {
                                telemetryError = error.Message;
                            }
                        }
                        var failedFeatureRead = false;
                        var featureReadAttempted = info is not null && !initialChannelsRead;
                        string? featureError = null;
                        var features = new List<ChannelStatus>();
                        if (featureReadAttempted)
                        {
                            for (var index = 0; index < Protocol.ChannelCount; index++)
                            {
                                try { features.Add(connection.ReadChannel(index)); }
                                catch (Exception error) when (HidErrors.IsDeviceError(error))
                                {
                                    failedFeatureRead = true;
                                    featureError ??= error.Message;
                                }
                            }
                        }
                        lock (gate)
                        {
                            if (!ReferenceEquals(client, connection)) break;
                            if (info is not null)
                            {
                                snapshot.Identity = info;
                                snapshot.IdentityError = null;
                            }
                            else if (identityError is not null)
                                snapshot.IdentityError = identityError;
                            if (configuration is not null)
                            {
                                snapshot.Configuration = configuration;
                                snapshot.ConfigurationError = null;
                            }
                            else if (configurationError is not null && needsConfiguration)
                                snapshot.ConfigurationError = configurationError;
                            ApplyTelemetry(info, telemetry, telemetryError);
                            foreach (var feature in features) snapshot.ApplyFeature(feature);
                            if (featureReadAttempted) snapshot.DiagnosticsError = featureError;
                            if (featureReadAttempted) initialChannelsRead = !failedFeatureRead;
                            nextMetadataRead = Stopwatch.GetTimestamp()
                                + Stopwatch.Frequency * snapshot.RefreshIntervalMs / 1000;
                        }
                    }
                    var status = connection.ReadStatus();
                    lock (gate)
                    {
                        if (!ReferenceEquals(client, connection)) break;
                        if (status is not null) snapshot.Apply(status);
                        snapshot.MarkStale();
                    }
                    await Task.Yield();
                }
            }
            catch (Exception error) when (HidErrors.IsDeviceError(error))
            {
                lock (gate)
                {
                    if (ReferenceEquals(client, connection))
                    {
                        client = null;
                        ReplaceSnapshot(new() { Error = error.Message });
                    }
                }
            }
            finally
            {
                lock (gate)
                    if (ReferenceEquals(client, connection)) client = null;
                if (connection is not null)
                {
                    try { await connection.DisposeAsync().AsTask().WaitAsync(stoppingToken); }
                    catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested) { }
                }
            }
            try { await Task.Delay(1000, stoppingToken); }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested) { break; }
        }
    }, stoppingToken);

    /// <summary>Retain an uncertain save result independently of the connection that submitted it.</summary>
    internal void RecordUnconfirmedConfigurationSaveError(string error)
    {
        lock (gate)
        {
            unconfirmedConfigurationSaveError = error;
            snapshot.ConfigurationSaveError = error;
        }
    }

    /// <summary>Replace per-connection state without discarding unresolved save uncertainty.</summary>
    internal void ReplaceSnapshot(DashboardSnapshot replacement)
    {
        lock (gate)
        {
            replacement.ConfigurationSaveError = unconfirmedConfigurationSaveError;
            snapshot = replacement;
        }
    }
}
