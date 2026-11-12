// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using HidSharp;
using HidSharp.Reports;
using System.Diagnostics;
using PicoUart.Wire;

namespace PicoUart.Hid;

public sealed record HidBoard(string DevicePath, string? SerialNumber, string ProductName);

internal sealed class HidStreamLifetime(Action dispose, string? devicePath = null)
{
    private static readonly object CleanupGate = new();
    private static readonly Dictionary<string, HashSet<Task>> PendingDeviceCleanup = new();
    private readonly object gate = new();
    private readonly TaskCompletionSource cleanup = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int activeTransfers;
    private bool disposeRequested;
    private bool disposeStarted;

    public Task Cleanup => cleanup.Task;

    public static void EnsureDeviceCleanupComplete(string devicePath)
    {
        while (true)
        {
            Task[] pending;
            lock (CleanupGate)
            {
                if (!PendingDeviceCleanup.TryGetValue(devicePath, out var tasks)) return;
                pending = tasks.ToArray();
            }

            var incomplete = pending.Any(task => !task.IsCompleted);
            if (incomplete)
                throw new IOException("previous HID cleanup is still pending; retry before opening this device");
            Task.WhenAll(pending).GetAwaiter().GetResult();

            lock (CleanupGate)
            {
                if (!PendingDeviceCleanup.TryGetValue(devicePath, out var tasks)) continue;
                foreach (var task in pending) tasks.Remove(task);
                if (tasks.Count == 0) PendingDeviceCleanup.Remove(devicePath);
            }
        }
    }

    public void TrackDeviceCleanup(string devicePath)
    {
        var completion = Cleanup;
        if (completion.IsCompletedSuccessfully) return;
        lock (CleanupGate)
        {
            if (!PendingDeviceCleanup.TryGetValue(devicePath, out var tasks))
                PendingDeviceCleanup.Add(devicePath, tasks = []);
            tasks.Add(completion);
        }
        _ = completion.ContinueWith(completed =>
        {
            if (completed.IsFaulted) _ = completed.Exception;
            if (!completed.IsCompletedSuccessfully) return;
            lock (CleanupGate)
            {
                if (!PendingDeviceCleanup.TryGetValue(devicePath, out var tasks)) return;
                tasks.Remove(completion);
                if (tasks.Count == 0) PendingDeviceCleanup.Remove(devicePath);
            }
        }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
    }

    public T Transfer<T>(Func<T> transfer, int timeoutMs)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(timeoutMs);
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposeRequested, this);
            activeTransfers++;
        }

        Task<T> operation;
        try
        {
            operation = Task.Run(() =>
            {
                try { return transfer(); }
                finally { CompleteTransfer(); }
            });
        }
        catch
        {
            CompleteTransfer();
            throw;
        }
        try { return operation.WaitAsync(TimeSpan.FromMilliseconds(timeoutMs)).GetAwaiter().GetResult(); }
        catch (TimeoutException)
        {
            if (devicePath is not null) TrackDeviceCleanup(devicePath);
            throw;
        }
    }

    public void Dispose()
    {
        bool disposeNow;
        lock (gate)
        {
            disposeRequested = true;
            disposeNow = activeTransfers == 0 && !disposeStarted;
            if (disposeNow) disposeStarted = true;
        }
        if (disposeNow) DisposeNative();
    }

    private void CompleteTransfer()
    {
        bool disposeNow;
        lock (gate)
        {
            activeTransfers--;
            disposeNow = disposeRequested && activeTransfers == 0 && !disposeStarted;
            if (disposeNow) disposeStarted = true;
        }
        if (disposeNow) DisposeNative();
    }

    private void DisposeNative()
    {
        try
        {
            dispose();
            cleanup.TrySetResult();
        }
        catch (Exception error)
        {
            cleanup.TrySetException(error);
            throw;
        }
    }
}

public sealed class PicoUartHid : IDisposable
{
    private readonly HidStream stream;
    private readonly HidStreamLifetime streamLifetime;
    private readonly string devicePath;
    private readonly object gate = new();
    private BoardIdentity? identity;
    private ConfigurationStatus? configuration;
    private bool disposed;
    private bool transferUnconfirmed;
    private bool rebootScheduled;
    internal ActionResult? LastActionResult { get; private set; }

    public PicoUartHid(string? serialNumber = null, string? devicePath = null)
    {
        if (serialNumber is not null && devicePath is not null)
            throw new ArgumentException("--serial and --device-path cannot be used together");
        var devices = MatchingDevices()
            .Where(device => devicePath is null || device.DevicePath == devicePath)
            .Where(device => serialNumber is null || device.GetSerialNumber() == serialNumber)
            .ToArray();
        if (devices.Length == 0)
            throw new InvalidOperationException("PicoUart HID interface not found with the expected usage metadata");
        if (devices.Length != 1)
            throw new InvalidOperationException("multiple PicoUart HID interfaces matched; use --serial or --device-path");
        var device = devices[0];
        HidStreamLifetime.EnsureDeviceCleanupComplete(device.DevicePath);
        this.devicePath = device.DevicePath;
        var descriptor = device.GetReportDescriptor();
        var identityReports = descriptor.DeviceItems.SelectMany(item => item.FeatureReports)
            .Where(report => report.ReportID == Protocol.IdentityReportId).ToArray();
        if (identityReports.Length != 1 || ReportDataLength(identityReports[0]) < 4)
            throw new InvalidOperationException("HID identity report is missing or has a truncated discovery prefix");
        stream = device.Open();
        streamLifetime = new(stream.Dispose, device.DevicePath);
        try
        {
            stream.ReadTimeout = 250;
            stream.WriteTimeout = 3000;
            var payload = ReadFeaturePayload(stream, streamLifetime,
                Protocol.IdentityReportId, ReportDataLength(identityReports[0]) - 1);
            var prefix = Protocol.ParseIdentityPrefix(payload);
            if (prefix.ProductId != Protocol.ProductId)
                throw new InvalidOperationException($"unsupported HID product (0x{prefix.ProductId:X4})");
            identity = Protocol.ParseIdentity(payload);
            ValidateUsbSerial(device.GetSerialNumber(), identity.DeviceUid);
            ValidateReports(descriptor.DeviceItems.SelectMany(item => item.FeatureReports)
                    .Select(report => (report.ReportID, ReportDataLength(report))),
                descriptor.DeviceItems.SelectMany(item => item.InputReports)
                    .Select(report => (report.ReportID, ReportDataLength(report))));
        }
        catch
        {
            streamLifetime.TrackDeviceCleanup(device.DevicePath);
            streamLifetime.Dispose();
            throw;
        }
    }

    private static IEnumerable<HidDevice> MatchingDevices() => DeviceList.Local.GetHidDevices(0x2E8A, 0x116B)
        .Where(device => device.GetReportDescriptor().DeviceItems
            .Any(item => item.Usages.GetAllValues().Contains(0xFF000001u)));

    public static HidBoard[] Discover() => MatchingDevices().Select(DiscoverDevice).ToArray();

    private static HidBoard DiscoverDevice(HidDevice device)
    {
        var features = device.GetReportDescriptor().DeviceItems.SelectMany(item => item.FeatureReports)
            .Where(report => report.ReportID == Protocol.IdentityReportId).ToArray();
        if (features.Length != 1 || ReportDataLength(features[0]) < 4)
            throw new InvalidOperationException("HID identity report is missing or has a truncated discovery prefix");
        HidStreamLifetime.EnsureDeviceCleanupComplete(device.DevicePath);
        var candidate = device.Open();
        var lifetime = new HidStreamLifetime(candidate.Dispose, device.DevicePath);
        try
        {
            candidate.ReadTimeout = 3000;
            candidate.WriteTimeout = 3000;
            var payload = ReadFeaturePayload(candidate, lifetime,
                Protocol.IdentityReportId, ReportDataLength(features[0]) - 1);
            var prefix = Protocol.ParseIdentityPrefix(payload);
            var usbSerial = device.GetSerialNumber();
            if (prefix.ProductId != Protocol.ProductId)
                return new(device.DevicePath, usbSerial, $"Unknown product (0x{prefix.ProductId:X4})");
            var identity = Protocol.ParseIdentity(payload);
            ValidateUsbSerial(usbSerial, identity.DeviceUid);
            return new(device.DevicePath, identity.DeviceUid, identity.ProductName);
        }
        finally
        {
            lifetime.TrackDeviceCleanup(device.DevicePath);
            lifetime.Dispose();
        }
    }

    private static void ValidateUsbSerial(string? usbSerial, string deviceUid)
    {
        if (usbSerial is not null && !string.Equals(usbSerial, deviceUid, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("HID identity UID does not match the USB serial number");
    }

    /// <summary>Report size including ID, excluding trailing constant padding reconstructed by Windows.</summary>
    internal static int ReportDataLength(Report report)
    {
        var bits = 0;
        var padding = false;
        foreach (var item in report.DataItems)
        {
            if (item.IsConstant)
            {
                padding = true;
                continue;
            }
            if (padding || item.TotalBits % 8 != 0)
                throw new InvalidOperationException("unsupported HID report data alignment or internal padding");
            bits = checked(bits + item.TotalBits);
        }
        return bits / 8 + 1;
    }

    internal static void ValidateReports(IEnumerable<(byte Id, int Length)> features,
        IEnumerable<(byte Id, int Length)> inputs)
    {
        (byte Id, int Length)[] expectedFeatures =
        [
            (Protocol.IdentityReportId, Protocol.IdentitySize + 1),
            (Protocol.ConfigurationReportId, Protocol.ConfigurationSize + 1),
            (Protocol.TelemetryReportId, Protocol.TelemetrySize + 1),
            (Protocol.ActionReportId, Protocol.ActionSize + 1),
            .. Enumerable.Range(0, Protocol.ChannelCount)
                .Select(index => ((byte)(Protocol.ChannelReportId + index), Protocol.ChannelSize + 1)),
            .. Enumerable.Range(0, Protocol.ChannelCount)
                .Select(index => ((byte)(Protocol.DiagnosticsReportId + index), Protocol.DiagnosticsSize + 1))
        ];
        var expectedInputs = Enumerable.Range(0, Protocol.ChannelCount)
            .Select(index => ((byte)(Protocol.ChannelReportId + index), Protocol.ChannelSize + 1));
        if (!features.OrderBy(report => report.Item1).SequenceEqual(expectedFeatures.OrderBy(report => report.Item1))
            || !inputs.OrderBy(report => report.Item1).SequenceEqual(expectedInputs.OrderBy(report => report.Item1)))
            throw new InvalidOperationException("unsupported HID report descriptor; consolidated v1 reports required");
    }

    public ChannelStatus? ReadStatus(int timeoutMs = 250)
        => ReadStatus(timeoutMs, timeoutMs);

    internal ChannelStatus? ReadStatus(int timeoutMs, int operationTimeoutMs)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(timeoutMs);
        if (timeoutMs > 0) ArgumentOutOfRangeException.ThrowIfNegativeOrZero(operationTimeoutMs);
        var timer = Stopwatch.StartNew();
        var lockTimeout = timeoutMs == 0 ? 0 : RemainingTimeoutMs(timer, operationTimeoutMs);
        if (!Monitor.TryEnter(gate, lockTimeout)) return null;
        try
        {
            CheckConnection();
            if (timeoutMs == 0) return null;
            var remainingMs = RemainingTimeoutMs(timer, operationTimeoutMs);
            if (remainingMs <= 0) return null;
            var info = ReadIdentity(remainingMs);
            remainingMs = RemainingTimeoutMs(timer, operationTimeoutMs);
            if (remainingMs <= 0) return null;
            var config = ReadConfiguration(remainingMs);
            remainingMs = RemainingTimeoutMs(timer, operationTimeoutMs);
            if (remainingMs <= 0) return null;
            stream.ReadTimeout = Math.Min(timeoutMs, remainingMs);
            var report = new byte[stream.Device.GetMaxInputReportLength()];
            int count;
            try { count = stream.Read(report, 0, report.Length); }
            catch (TimeoutException) { return null; }
            if (count != Protocol.ChannelSize + 1)
                throw new InvalidOperationException("unexpected HID channel report size");
            return Protocol.ParseChannel(report[0], report.AsSpan(1, count - 1), info, config);
        }
        finally
        {
            Monitor.Exit(gate);
        }
    }

    public BoardIdentity ReadIdentity() => ReadIdentity(3000);

    private BoardIdentity ReadIdentity(int timeoutMs)
    {
        lock (gate)
        {
            CheckConnection();
            return identity ??= Protocol.ParseIdentity(
                ReadFeature(Protocol.IdentityReportId, Protocol.IdentitySize, timeoutMs));
        }
    }

    public ConfigurationStatus ReadConfiguration() => ReadConfiguration(3000);

    private ConfigurationStatus ReadConfiguration(int timeoutMs)
    {
        lock (gate)
        {
            var info = ReadIdentity(timeoutMs);
            return configuration ??= Protocol.ParseConfiguration(
                ReadFeature(Protocol.ConfigurationReportId, Protocol.ConfigurationSize, timeoutMs), info);
        }
    }

    public Telemetry ReadTelemetry()
    {
        lock (gate)
        {
            var info = ReadIdentity();
            return Protocol.ParseTelemetry(ReadFeature(Protocol.TelemetryReportId, Protocol.TelemetrySize), info);
        }
    }

    public BoardStatus ReadBoardStatus()
    {
        lock (gate)
        {
            var info = ReadIdentity();
            var telemetry = ReadTelemetry();
            return new(telemetry.TemperatureCelsius, info.FirmwareVersion,
                info.FirmwareMajor, info.FirmwareMinor, info.FirmwarePatch, (info.Capabilities & 2) != 0);
        }
    }

    public HardwareInfo ReadHardwareInfo()
    {
        lock (gate)
        {
            var info = ReadIdentity();
            return new(info.Mcu, ReadTelemetry().SystemClockHz, info.McuId);
        }
    }

    public ActionResult ReadActionResult(int timeoutMs = 3000) =>
        ReadActionResultCore(timeoutMs);

    public ConfigurationStatus ReadBootConfiguration() => ReadConfiguration();
    public RuntimeStatus ReadRuntime() => ReadTelemetry().Runtime;
    public LedStatus ReadLed() => ReadTelemetry().Led;
    public uint[] ReadOverflowCounts() => ReadChannelDiagnostics()
        .Select(channel => channel.RxOverrunCount).ToArray();

    /// <summary>Non-consuming Feature sample; never add its traffic to interrupt totals.</summary>
    public ChannelStatus ReadChannel(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index, Protocol.ChannelCount);
        lock (gate)
        {
            var id = (byte)(Protocol.ChannelReportId + index);
            return Protocol.ParseChannel(id, ReadFeature(id, Protocol.ChannelSize),
                ReadIdentity(), ReadConfiguration());
        }
    }

    public ChannelDiagnostics ReadChannelDiagnostic(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index, Protocol.ChannelCount);
        lock (gate)
        {
            var id = (byte)(Protocol.DiagnosticsReportId + index);
            return Protocol.ParseDiagnostics(id, ReadFeature(id, Protocol.DiagnosticsSize), ReadConfiguration());
        }
    }

    public ChannelDiagnostics[] ReadChannelDiagnostics() =>
        Enumerable.Range(0, Protocol.ChannelCount).Select(ReadChannelDiagnostic).ToArray();

    public ActionResult ToggleLed()
    {
        lock (gate)
        {
            var output = ReadLed().OutputOn
                ?? throw new InvalidOperationException("board identification LED unavailable");
            return SendAction(output ? (byte)1 : (byte)2);
        }
    }

    public ActionResult ResetBoard()
    {
        lock (gate) return SendAction(3);
    }

    /// <summary>Return only after report 20 confirms the save and automatic reboot schedule.</summary>
    public ConfigurationSaveStatus SaveConfiguration(uint profile, bool unsafeOverclock)
    {
        lock (gate)
        {
            var result = SendAction(4, profile, unsafeOverclock);
            return new(result.Status, result.ErrorReason, profile);
        }
    }

    private ActionResult SendAction(byte action, uint profile = 0, bool unsafeOverclock = false)
    {
        CheckConnection();
        if (rebootScheduled) throw new InvalidOperationException("board reboot already scheduled; reconnect");
        var info = ReadIdentity();
        LastActionResult = null;
        var prior = ReadActionResultCore(3000);
        if (prior.Status == 1 || (prior.Status == 2 && prior.Action is 3 or 4))
            throw new InvalidOperationException("a board action is pending or reboot is already scheduled; reconnect");
        var payload = Protocol.BuildAction(action, info, profile, unsafeOverclock);
        byte[] report = [Protocol.ActionReportId, .. payload];
        var submissionAttempted = false;
        try
        {
            submissionAttempted = true;
            Transfer(() => { stream.SetFeature(report); return true; }, 3000);
            var result = WaitForAction(action, timeout =>
            {
                var observed = ReadActionResultCore(timeout);
                LastActionResult = observed;
                return observed;
            });
            rebootScheduled = action is 3 or 4;
            return result;
        }
        catch (ControlRequestRejectedException)
        {
            throw;
        }
        catch (Exception error) when (submissionAttempted && HidErrors.IsDeviceError(error))
        {
            transferUnconfirmed = true;
            throw Unconfirmed("command outcome is unconfirmed; do not retry; reconnect and verify board state", error);
        }
    }

    internal static ActionResult WaitForAction(byte action, Func<int, ActionResult> read, int timeoutMs = 3000)
    {
        var timer = Stopwatch.StartNew();
        while (true)
        {
            var remaining = timeoutMs - (int)timer.ElapsedMilliseconds;
            if (remaining <= 0)
                throw Unconfirmed("board action outcome is unconfirmed; reconnect before further I/O");
            var result = read(remaining);
            if (timer.ElapsedMilliseconds >= timeoutMs)
                throw Unconfirmed("board action outcome is unconfirmed; reconnect before further I/O");
            if (result.Action != action)
                throw Unconfirmed("board action outcome is unconfirmed: result belongs to another action");
            if (result.Status == 2) return result;
            if (result.Status is >= 3 and <= 6)
                throw new ControlRequestRejectedException(
                    $"board action rejected: status {result.Status}, error {result.ErrorReason}"
                    + (result.Status == 5 ? "; stop UART traffic and close all CDC ports" : ""));
            if (result.Status != 1)
                throw Unconfirmed("board action outcome is unconfirmed: no matching result");
            remaining = timeoutMs - (int)timer.ElapsedMilliseconds;
            if (remaining > 0) Thread.Sleep(Math.Min(50, remaining));
        }
    }

    public void Dispose()
    {
        lock (gate)
        {
            if (disposed) return;
            disposed = true;
            identity = null;
            configuration = null;
            streamLifetime.TrackDeviceCleanup(devicePath);
            streamLifetime.Dispose();
        }
    }

    public async ValueTask DisposeAsync()
    {
        Dispose();
        await streamLifetime.Cleanup.ConfigureAwait(false);
    }

    private void CheckConnection()
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        if (transferUnconfirmed)
            throw new IOException("previous HID operation is unconfirmed; reconnect before further I/O");
    }

    private T Transfer<T>(Func<T> transfer, int timeoutMs)
    {
        CheckConnection();
        try { return streamLifetime.Transfer(transfer, timeoutMs); }
        catch (TimeoutException)
        {
            transferUnconfirmed = true;
            throw;
        }
    }

    private byte[] ReadFeature(byte reportId, int payloadSize, int timeoutMs = 3000)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(timeoutMs);
        lock (gate)
        {
            CheckConnection();
            try { return ReadFeaturePayload(stream, streamLifetime, reportId, payloadSize, timeoutMs); }
            catch (TimeoutException)
            {
                transferUnconfirmed = true;
                throw;
            }
        }
    }

    private static byte[] ReadFeaturePayload(HidStream target, HidStreamLifetime lifetime,
        byte reportId, int payloadSize, int timeoutMs = 3000)
    {
        var prefixSize = OperatingSystem.IsLinux() ? 2 : 1;
        var report = new byte[Math.Max(payloadSize + prefixSize,
            target.Device.GetMaxFeatureReportLength() + prefixSize - 1)];
        report[0] = reportId;
        lifetime.Transfer(() => { target.GetFeature(report); return true; }, timeoutMs);
        return FeaturePayload(report, reportId, payloadSize, prefixSize);
    }

    internal static byte[] FeaturePayload(byte[] report, byte reportId, int payloadSize, int prefixSize)
    {
        if (report.Length < payloadSize + prefixSize || report[0] != reportId
            || (prefixSize == 2 && report[1] != reportId))
            throw new InvalidOperationException("unexpected HID feature report ID or size");
        return report.AsSpan(prefixSize, payloadSize).ToArray();
    }

    private ActionResult ReadActionResultCore(int timeoutMs) =>
        Protocol.ParseActionResult(ReadFeature(Protocol.ActionReportId, Protocol.ActionSize, timeoutMs));

    private static int RemainingTimeoutMs(Stopwatch timer, int timeoutMs) =>
        Math.Max(0, (int)(timeoutMs - timer.Elapsed.TotalMilliseconds));

    private static ControlOutcomeUnconfirmedException Unconfirmed(string message, Exception? innerException = null) =>
        new(message, innerException);
}
