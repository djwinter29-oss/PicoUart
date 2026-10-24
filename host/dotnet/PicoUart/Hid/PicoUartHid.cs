using HidSharp;
using System.Diagnostics;
using PicoUart.Wire;

namespace PicoUart.Hid;

public sealed record HidBoard(string DevicePath, string? SerialNumber, string ProductName);

internal sealed class HidStreamLifetime(Action dispose)
{
    private readonly object gate = new();
    private int activeTransfers;
    private bool disposeRequested;
    private bool disposeStarted;

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
        return operation.WaitAsync(TimeSpan.FromMilliseconds(timeoutMs)).GetAwaiter().GetResult();
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
        if (disposeNow) dispose();
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
        if (disposeNow) dispose();
    }
}

public sealed class PicoUartHid : IDisposable
{
    private readonly HidStream stream;
    private readonly HidStreamLifetime streamLifetime;
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
        var descriptor = device.GetReportDescriptor();
        var identityReports = descriptor.DeviceItems.SelectMany(item => item.FeatureReports)
            .Where(report => report.ReportID == Protocol.IdentityReportId).ToArray();
        if (identityReports.Length != 1 || identityReports[0].Length < 4)
            throw new InvalidOperationException("HID identity report is missing or has a truncated discovery prefix");
        stream = device.Open();
        streamLifetime = new(stream.Dispose);
        try
        {
            stream.ReadTimeout = 250;
            stream.WriteTimeout = 3000;
            var payload = ReadFeaturePayload(stream, streamLifetime,
                Protocol.IdentityReportId, identityReports[0].Length - 1);
            var prefix = Protocol.ParseIdentityPrefix(payload);
            if (prefix.ProductId != Protocol.ProductId)
                throw new InvalidOperationException($"unsupported HID product (0x{prefix.ProductId:X4})");
            identity = Protocol.ParseIdentity(payload);
            ValidateUsbSerial(device.GetSerialNumber(), identity.DeviceUid);
            ValidateReports(descriptor.DeviceItems.SelectMany(item => item.FeatureReports)
                    .Select(report => ((byte)report.ReportID, report.Length)),
                descriptor.DeviceItems.SelectMany(item => item.InputReports)
                    .Select(report => ((byte)report.ReportID, report.Length)));
        }
        catch
        {
            streamLifetime.Dispose();
            throw;
        }
    }

    private static IEnumerable<HidDevice> MatchingDevices() => DeviceList.Local.GetHidDevices(0xCAFE, 0x4010)
        .Where(device => device.GetReportDescriptor().DeviceItems
            .Any(item => item.Usages.GetAllValues().Contains(0xFF000001u)));

    public static HidBoard[] Discover() => MatchingDevices().Select(DiscoverDevice).ToArray();

    private static HidBoard DiscoverDevice(HidDevice device)
    {
        var features = device.GetReportDescriptor().DeviceItems.SelectMany(item => item.FeatureReports)
            .Where(report => report.ReportID == Protocol.IdentityReportId).ToArray();
        if (features.Length != 1 || features[0].Length < 4)
            throw new InvalidOperationException("HID identity report is missing or has a truncated discovery prefix");
        var candidate = device.Open();
        var lifetime = new HidStreamLifetime(candidate.Dispose);
        try
        {
            candidate.ReadTimeout = 3000;
            candidate.WriteTimeout = 3000;
            var payload = ReadFeaturePayload(candidate, lifetime,
                Protocol.IdentityReportId, features[0].Length - 1);
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
            lifetime.Dispose();
        }
    }

    private static void ValidateUsbSerial(string? usbSerial, string deviceUid)
    {
        if (usbSerial is not null && !string.Equals(usbSerial, deviceUid, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("HID identity UID does not match the USB serial number");
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
    {
        ArgumentOutOfRangeException.ThrowIfNegative(timeoutMs);
        lock (gate)
        {
            CheckConnection();
            if (timeoutMs == 0) return null;
            stream.ReadTimeout = timeoutMs;
            var report = new byte[stream.Device.GetMaxInputReportLength()];
            int count;
            try { count = stream.Read(report, 0, report.Length); }
            catch (TimeoutException) { return null; }
            if (count != Protocol.ChannelSize + 1)
                throw new InvalidOperationException("unexpected HID channel report size");
            var info = ReadIdentity();
            var config = ReadConfiguration();
            return Protocol.ParseChannel(report[0], report.AsSpan(1, count - 1), info, config);
        }
    }

    public BoardIdentity ReadIdentity()
    {
        lock (gate)
        {
            CheckConnection();
            return identity ??= Protocol.ParseIdentity(ReadFeature(Protocol.IdentityReportId, Protocol.IdentitySize));
        }
    }

    public ConfigurationStatus ReadConfiguration()
    {
        lock (gate)
        {
            var info = ReadIdentity();
            return configuration ??= Protocol.ParseConfiguration(
                ReadFeature(Protocol.ConfigurationReportId, Protocol.ConfigurationSize), info);
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
            var result = SendAction(output ? (byte)1 : (byte)2);
            ReadTelemetry();
            return result;
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
        try
        {
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
        catch (Exception error) when (error is TimeoutException or IOException)
        {
            transferUnconfirmed = true;
            throw;
        }
    }

    internal static ActionResult WaitForAction(byte action, Func<int, ActionResult> read, int timeoutMs = 3000)
    {
        var timer = Stopwatch.StartNew();
        while (true)
        {
            var remaining = timeoutMs - (int)timer.ElapsedMilliseconds;
            if (remaining <= 0) throw Unconfirmed("board action outcome unconfirmed; reconnect before further I/O");
            var result = read(remaining);
            if (timer.ElapsedMilliseconds >= timeoutMs)
                throw Unconfirmed("board action outcome unconfirmed; reconnect before further I/O");
            if (result.Action != action)
                throw Unconfirmed("board action outcome unconfirmed: result belongs to another action");
            if (result.Status == 2) return result;
            if (result.Status is >= 3 and <= 6)
                throw new InvalidOperationException(
                    $"board action rejected: status {result.Status}, error {result.ErrorReason}"
                    + (result.Status == 5 ? "; stop UART traffic and close all CDC ports" : ""));
            if (result.Status != 1)
                throw Unconfirmed("board action outcome unconfirmed: no matching result");
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
            streamLifetime.Dispose();
        }
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

    private static IOException Unconfirmed(string message) => new(message);
}
