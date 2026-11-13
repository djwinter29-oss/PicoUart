// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Diagnostics;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using PicoUart.Hid;
using PicoUart.Protocol;

namespace PicoUart.Dashboard;

public sealed class BoardMonitor : BackgroundService
{
    private static readonly TimeSpan ControlQueueTimeout = TimeSpan.FromSeconds(5);
    private readonly object gate = new();
    private readonly SemaphoreSlim controlGate = new(1, 1);
    private readonly Func<Client> connect;
    private readonly ILogger<BoardMonitor> logger;
    private Snapshot snapshot = new();
    private Client? client;
    private string? unconfirmedConfigurationSaveError;

    public BoardMonitor(string? serialNumber, string? devicePath, ILogger<BoardMonitor>? logger = null)
        : this(() => new Client(serialNumber, devicePath), logger) { }

    internal BoardMonitor(Func<Client> connect, ILogger<BoardMonitor>? logger = null)
    {
        this.connect = connect;
        this.logger = logger ?? NullLogger<BoardMonitor>.Instance;
    }

    public string SnapshotJson()
    {
        lock (gate)
        {
            snapshot.MarkStale();
            return JsonSerializer.Serialize(snapshot, Codec.JsonOptions);
        }
    }

    public BoardControlStatus Action(string action, CancellationToken cancellationToken = default)
    {
        WaitForControl(cancellationToken);
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            var connection = Connection();
            try
            {
                var result = action switch
                {
                    "toggle-led" => connection.ToggleLed(cancellationToken),
                    "reset" => connection.ResetBoard(cancellationToken),
                    _ => throw new ArgumentException("unknown board action", nameof(action))
                };
                lock (gate)
                    if (ReferenceEquals(client, connection)) snapshot.ApplyAction(result);
                if (action == "toggle-led") ReadTelemetry(connection);
                return ToControlStatus(result);
            }
            catch (Exception error) when (Errors.IsDeviceError(error) || error is ArgumentException)
            {
                var result = error switch
                {
                    ControlRequestRejectedException rejected => rejected.Result,
                    ControlOutcomeUnconfirmedException unconfirmed => unconfirmed.Result,
                    _ => null
                };
                lock (gate)
                {
                    if (ReferenceEquals(client, connection))
                    {
                        if (result is not null) snapshot.ApplyAction(result);
                        if (action == "toggle-led" && error is ControlOutcomeUnconfirmedException)
                        {
                            snapshot.Led = null;
                            snapshot.LedError = error.Message;
                        }
                        else if (action == "toggle-led" && result is null)
                            snapshot.LedError = error.Message;
                    }
                }
                throw;
            }
        }
        finally { controlGate.Release(); }
    }

    public BoardControlStatus SaveConfiguration(
        uint profile, bool unsafeOverclock, CancellationToken cancellationToken = default)
    {
        WaitForControl(cancellationToken);
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            var connection = Connection();
            try
            {
                var result = connection.SaveConfiguration(profile, unsafeOverclock, cancellationToken);
                lock (gate)
                {
                    if (ReferenceEquals(client, connection))
                    {
                        snapshot.ApplyAction(new(4, result.Status, result.ErrorReason));
                        snapshot.ConfigurationSave = result;
                        snapshot.ConfigurationSaveError = null;
                        snapshot.ConfigurationSaveUnconfirmed = false;
                        unconfirmedConfigurationSaveError = null;
                    }
                }
                return ToControlStatus(new(4, result.Status, result.ErrorReason));
            }
            catch (Exception error) when (Errors.IsDeviceError(error) || error is ArgumentException)
            {
                if (error is ControlOutcomeUnconfirmedException)
                    RecordUnconfirmedConfigurationSaveError(error.Message);
                var result = error switch
                {
                    ControlRequestRejectedException rejected => rejected.Result,
                    ControlOutcomeUnconfirmedException unconfirmed => unconfirmed.Result,
                    _ => null
                };
                lock (gate)
                {
                    if (ReferenceEquals(client, connection))
                    {
                        if (result?.Action == 4)
                        {
                            snapshot.ApplyAction(result);
                            snapshot.ConfigurationSave = new(result.Status, result.ErrorReason, profile);
                        }
                        snapshot.ConfigurationSaveError = error.Message;
                        snapshot.ConfigurationSaveUnconfirmed = unconfirmedConfigurationSaveError is not null;
                    }
                }
                throw;
            }
        }
        finally { controlGate.Release(); }
    }

    public string DiagnosticsJson()
    {
        var connection = Connection();
        try
        {
            return PublishDiagnostics(connection, connection.ReadChannelDiagnostics());
        }
        catch (Exception error) when (Errors.IsDeviceError(error))
        {
            lock (gate)
                if (ReferenceEquals(client, connection)) snapshot.DiagnosticsError = error.Message;
            logger.LogWarning(error, "HID diagnostics refresh failed; retaining the last successful diagnostics");
            throw;
        }
    }

    internal string PublishDiagnostics(Client connection, ChannelDiagnostics[] diagnostics)
    {
        lock (gate)
        {
            if (!ReferenceEquals(client, connection))
                throw new IOException("board connection changed during diagnostics; retry the request");
            Array.Copy(diagnostics, snapshot.ChannelDiagnostics, diagnostics.Length);
            snapshot.DiagnosticsError = null;
            return JsonSerializer.Serialize(diagnostics, Codec.JsonOptions);
        }
    }

    private Client Connection()
    {
        lock (gate)
            return client ?? throw new InvalidOperationException(
                snapshot.Error ?? "PicoUart HID interface is not connected");
    }

    private void WaitForControl(CancellationToken cancellationToken)
    {
        if (!controlGate.Wait(ControlQueueTimeout, cancellationToken))
            throw new TimeoutException("timed out waiting for another board control operation");
    }

    private static BoardControlStatus ToControlStatus(ActionResult result) =>
        new(result.Action, result.Status, result.ErrorReason,
            result.Status is >= 3 and <= 6
                ? $"board action {Codec.ActionStatusName(result.Status)} (error {result.ErrorReason})"
                : null);

    private void ReadControl(Client connection)
    {
        controlGate.Wait();
        try
        {
            ActionResult? result = null;
            string? errorMessage = null;
            try
            {
                var identity = connection.ReadIdentity();
                if ((identity.Capabilities & 4) == 0)
                    errorMessage = "configuration save capability is unavailable";
                else
                    result = connection.ReadActionResult();
            }
            catch (Exception error) when (Errors.IsDeviceError(error))
            {
                errorMessage = error.Message;
            }
            lock (gate)
            {
                if (!ReferenceEquals(client, connection)) return;
                if (unconfirmedConfigurationSaveError is not null)
                {
                    snapshot.ConfigurationSave = null;
                    snapshot.ConfigurationSaveError = unconfirmedConfigurationSaveError;
                    snapshot.ConfigurationSaveUnconfirmed = true;
                    return;
                }
                if (errorMessage is not null)
                {
                    if (snapshot.ConfigurationSaveError != errorMessage)
                        logger.LogWarning("HID control result unavailable: {Error}", errorMessage);
                    snapshot.ConfigurationSave = null;
                    snapshot.ConfigurationSaveError = errorMessage;
                    snapshot.ConfigurationSaveUnconfirmed = false;
                    return;
                }
                if (result is null) return;
                var previous = snapshot.ConfigurationSave;
                snapshot.ApplyAction(result);
                if (result.Action != 4)
                    snapshot.ConfigurationSave = new(0, 0, null);
                else if (previous?.Status == result.Status && previous.ErrorReason == result.ErrorReason)
                    snapshot.ConfigurationSave = previous;
                snapshot.ConfigurationSaveError = result.Action == 4 ? snapshot.Control?.Error : null;
                snapshot.ConfigurationSaveUnconfirmed = false;
            }
        }
        finally { controlGate.Release(); }
    }

    private void ReadTelemetry(Client connection)
    {
        BoardIdentity? info = null;
        Telemetry? telemetry = null;
        string? errorMessage = null;
        try
        {
            info = connection.ReadIdentity();
            telemetry = connection.ReadTelemetry();
        }
        catch (Exception error) when (Errors.IsDeviceError(error))
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
        string? lastConnectionError = null;
        while (!stoppingToken.IsCancellationRequested)
        {
            Client? connection = null;
            try
            {
                connection = connect();
                lastConnectionError = null;
                logger.LogInformation("PicoUart HID connected: {DeviceUid}", connection.ReadIdentity().DeviceUid);
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
                        catch (Exception error) when (Errors.IsDeviceError(error))
                        {
                            identityError = error.Message;
                        }
                        if (info is not null)
                        {
                            if (needsConfiguration)
                            {
                                try { configuration = connection.ReadConfiguration(); }
                                catch (Exception error) when (Errors.IsDeviceError(error))
                                {
                                    configurationError = error.Message;
                                }
                            }
                            try { telemetry = connection.ReadTelemetry(); }
                            catch (Exception error) when (Errors.IsDeviceError(error))
                            {
                                telemetryError = error.Message;
                            }
                        }
                        var failedFeatureRead = false;
                        var featureReadAttempted = info is not null && !initialChannelsRead;
                        var features = new List<ChannelStatus>();
                        if (featureReadAttempted)
                        {
                            for (var index = 0; index < Codec.ChannelCount; index++)
                            {
                                try { features.Add(connection.ReadChannel(index)); }
                                catch (Exception error) when (Errors.IsDeviceError(error))
                                {
                                    failedFeatureRead = true;
                                    logger.LogWarning(error,
                                        "Initial channel Feature read failed for CDC{Channel}", index);
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
                            if (featureReadAttempted) initialChannelsRead = !failedFeatureRead;
                            nextMetadataRead = Stopwatch.GetTimestamp()
                                + Stopwatch.Frequency * snapshot.RefreshIntervalMs / 1000;
                        }
                        ReadControl(connection);
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
            catch (Exception error) when (Errors.IsDeviceError(error))
            {
                if (lastConnectionError != error.Message)
                    logger.LogWarning(error, "PicoUart HID unavailable; reconnect pending");
                lastConnectionError = error.Message;
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
                    try
                    {
                        var cleanup = connection.DisposeAsync();
                        if (!cleanup.IsCompleted)
                            logger.LogWarning("Waiting for in-flight native HID transfers before reconnecting");
                        await cleanup.AsTask().WaitAsync(stoppingToken);
                    }
                    catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested) { }
                    catch (Exception error) when (Errors.IsDeviceError(error))
                    {
                        logger.LogError(error, "Native HID cleanup failed");
                        throw;
                    }
                }
            }
            try { await Task.Delay(1000, stoppingToken); }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested) { break; }
        }
    }, stoppingToken);

    /// <summary>Retain an uncertain save result independently of the connection that submitted it.</summary>
    internal void RecordUnconfirmedConfigurationSaveError(string error)
    {
        logger.LogWarning("HID save outcome unconfirmed; do not retry automatically: {Error}", error);
        lock (gate)
        {
            unconfirmedConfigurationSaveError = error;
            snapshot.ConfigurationSaveError = error;
            snapshot.ConfigurationSaveUnconfirmed = true;
        }
    }

    /// <summary>Replace per-connection state without discarding unresolved save uncertainty.</summary>
    internal void ReplaceSnapshot(Snapshot replacement)
    {
        lock (gate)
        {
            replacement.ConfigurationSaveError = unconfirmedConfigurationSaveError;
            replacement.ConfigurationSaveUnconfirmed = unconfirmedConfigurationSaveError is not null;
            snapshot = replacement;
        }
    }
}
