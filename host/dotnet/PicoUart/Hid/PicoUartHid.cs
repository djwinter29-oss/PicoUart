using HidSharp;
using System.Diagnostics;
using PicoUart.Wire;

namespace PicoUart.Hid;

public sealed record HidBoard(string DevicePath, string? SerialNumber, string ProductName);

public sealed class PicoUartHid : IDisposable
{
    private readonly HidStream stream;
    private readonly object gate = new();
    private BoardIdentity? identity;
    private ulong nextRequestId = 1;
    private bool disposed;
    private bool transferUnconfirmed;
    private bool rebootScheduled;
    internal ControlResult? LastCommandResult { get; private set; }

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
        var descriptor = devices[0].GetReportDescriptor();
        ValidateReports(descriptor.DeviceItems.SelectMany(item => item.FeatureReports)
                .Select(report => ((byte)report.ReportID, report.Length)),
            descriptor.DeviceItems.SelectMany(item => item.InputReports)
                .Select(report => ((byte)report.ReportID, report.Length)));
        stream = devices[0].Open();
        stream.ReadTimeout = 250;
        stream.WriteTimeout = 3000;
    }

    private static IEnumerable<HidDevice> MatchingDevices() => DeviceList.Local.GetHidDevices(0xCAFE, 0x4010)
        .Where(device => device.GetReportDescriptor().DeviceItems
            .Any(item => item.Usages.GetAllValues().Contains(0xFF000001u)));

    public static HidBoard[] Discover() => MatchingDevices().Select(device =>
        new HidBoard(device.DevicePath, device.GetSerialNumber(), device.GetProductName())).ToArray();

    internal static void ValidateReports(IEnumerable<(byte Id, int Length)> features,
        IEnumerable<(byte Id, int Length)> inputs)
    {
        (byte Id, int Length)[] expectedFeatures =
        [
            (Protocol.IdentityReportId, Protocol.IdentitySize + 1),
            (Protocol.ControlReportId, Protocol.ControlSize + 1),
            (Protocol.TelemetryReportId, Protocol.TelemetrySize + 1),
            .. Enumerable.Range(0, Protocol.ChannelCount)
                .Select(index => ((byte)(Protocol.ChannelReportId + index), Protocol.ChannelSize + 1))
        ];
        var expectedInputs = Enumerable.Range(0, Protocol.ChannelCount)
            .Select(index => ((byte)(Protocol.ChannelReportId + index), Protocol.ChannelSize + 1));
        if (!features.OrderBy(report => report.Id).SequenceEqual(expectedFeatures)
            || !inputs.OrderBy(report => report.Id).SequenceEqual(expectedInputs))
            throw new InvalidOperationException("unsupported HID report descriptor; consolidated v1 reports required");
    }

    public ChannelStatus? ReadStatus(int timeoutMs = 250)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(timeoutMs);
        lock (gate)
        {
            CheckConnection();
            stream.ReadTimeout = timeoutMs;
            var report = new byte[stream.Device.GetMaxInputReportLength()];
            int count;
            try { count = stream.Read(report, 0, report.Length); }
            catch (TimeoutException) { return null; }
            if (count != Protocol.ChannelSize + 1)
                throw new InvalidOperationException("unexpected HID channel report size");
            return Protocol.ParseChannel(report[0], report.AsSpan(1, count - 1));
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
            return new(ReadTelemetry().TemperatureCelsius, info.FirmwareVersion,
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

    public ControlResult ReadControl(int timeoutMs = 3000)
    {
        lock (gate)
        {
            var info = ReadIdentity();
            return Protocol.ParseControl(ReadFeature(Protocol.ControlReportId, Protocol.ControlSize, timeoutMs), info);
        }
    }

    public ConfigurationStatus ReadConfiguration() => ReadControl().Configuration;
    public RuntimeStatus ReadRuntime() => ReadTelemetry().Runtime;
    public LedStatus ReadLed() => ReadTelemetry().Led;
    public uint[] ReadOverflowCounts() => ReadChannelDiagnostics()
        .Select(channel => channel.Faults["rx_overrun"].Count).ToArray();

    /// <summary>Non-consuming Feature sample; never add its traffic to interrupt totals.</summary>
    public ChannelStatus ReadChannel(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index, Protocol.ChannelCount);
        var id = (byte)(Protocol.ChannelReportId + index);
        return Protocol.ParseChannel(id, ReadFeature(id, Protocol.ChannelSize));
    }

    public ChannelDiagnostics ReadChannelDiagnostic(int index) => ReadChannel(index).Diagnostics;
    public ChannelDiagnostics[] ReadChannelDiagnostics() => Enumerable.Range(0, Protocol.ChannelCount)
        .Select(ReadChannelDiagnostic).ToArray();

    public void ToggleLed()
    {
        lock (gate)
        {
            var output = ReadLed().OutputOn
                ?? throw new InvalidOperationException("board identification LED unavailable");
            SendCommand(output ? (byte)1 : (byte)2);
            ReadTelemetry();
        }
    }

    public void ResetBoard()
    {
        lock (gate)
        {
            SendCommand(3);
            SendCommand(4);
        }
    }

    /// <summary>Return only after verified completion has scheduled the automatic reboot.</summary>
    public ConfigurationSaveStatus SaveConfiguration(uint profile, bool unsafeOverclock)
    {
        lock (gate)
        {
            var result = SendCommand(5, profile, unsafeOverclock);
            return new(result.Status, result.RequestedProfile);
        }
    }

    private ControlResult SendCommand(byte command, uint profile = 0, bool unsafeOverclock = false)
    {
        CheckConnection();
        if (rebootScheduled) throw new InvalidOperationException("board reboot already scheduled; reconnect");
        var info = ReadIdentity();
        var latest = ReadControl();
        if (latest.Status == 1 || (latest.Status == 2 && latest.Command is 4 or 5))
            throw new InvalidOperationException("a board command is pending or reboot is already scheduled; reconnect");
        var id = AllocateRequestId(ref nextRequestId, latest.RequestId);
        var payload = Protocol.BuildCommand(command, id, info, profile, unsafeOverclock);
        byte[] report = [Protocol.ControlReportId, .. payload];
        LastCommandResult = null;
        try
        {
            Transfer(() => { stream.SetFeature(report); return true; }, 3000);
            var result = WaitForCommand(command, payload[2], id, command == 5 ? profile : null, timeout =>
            {
                var observed = ReadControl(timeout);
                if (observed.Command == command && observed.RequestId == id && observed.Flags == payload[2])
                    LastCommandResult = observed;
                return observed;
            });
            rebootScheduled = command is 4 or 5;
            return result;
        }
        catch (Exception error) when (error is TimeoutException or IOException)
        {
            transferUnconfirmed = true;
            throw;
        }
    }

    internal static uint AllocateRequestId(ref ulong next, uint latest)
    {
        if (next == latest) next++;
        if (next > uint.MaxValue)
            throw new InvalidOperationException("control request ID space exhausted; reconnect");
        return (uint)next++;
    }

    internal static ControlResult WaitForCommand(byte command, byte flags, uint requestId, uint? profile,
        Func<int, ControlResult> read, int timeoutMs = 3000)
    {
        var elapsed = Stopwatch.StartNew();
        while (true)
        {
            var remaining = timeoutMs - (int)elapsed.ElapsedMilliseconds;
            if (remaining <= 0) throw new TimeoutException("board command outcome unconfirmed; reconnect and inspect configuration");
            var result = read(remaining);
            if (elapsed.ElapsedMilliseconds >= timeoutMs)
                throw new TimeoutException("board command outcome unconfirmed; reconnect and inspect configuration");
            if (result.RequestId != requestId || result.Command != command || result.Flags != flags)
                throw new IOException("board command outcome unconfirmed: result belongs to another request");
            if (result.Status is 1 or 2 && result.RequestedProfile != profile)
                throw new IOException("board command outcome unconfirmed: requested profile mismatch");
            if (result.Status == 2) return result;
            if (result.Status >= 3)
                throw new InvalidOperationException($"board command rejected: status {result.Status}, error {result.ErrorReason}"
                    + (result.Status == 5 ? "; stop UART traffic and close all CDC ports" : ""));
            if (result.Status != 1) throw new IOException("board command outcome unconfirmed: idle result");
            remaining = timeoutMs - (int)elapsed.ElapsedMilliseconds;
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
            stream.Dispose();
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
        try
        {
            // HidSharp feature transfers have no portable cancellation; retire the connection after a deadline.
            return Task.Run(transfer).WaitAsync(TimeSpan.FromMilliseconds(timeoutMs)).GetAwaiter().GetResult();
        }
        catch (TimeoutException)
        {
            transferUnconfirmed = true;
            throw;
        }
    }

    private byte[] ReadFeature(byte reportId, int payloadSize, int timeoutMs = 3000)
    {
        lock (gate)
        {
            CheckConnection();
            var prefixSize = OperatingSystem.IsLinux() ? 2 : 1;
            var report = new byte[Math.Max(payloadSize + prefixSize,
                stream.Device.GetMaxFeatureReportLength() + prefixSize - 1)];
            report[0] = reportId;
            Transfer(() => { stream.GetFeature(report); return true; }, timeoutMs);
            return FeaturePayload(report, reportId, payloadSize, prefixSize);
        }
    }

    internal static byte[] FeaturePayload(byte[] report, byte reportId, int payloadSize, int prefixSize)
    {
        if (report.Length < payloadSize + prefixSize || report[0] != reportId
            || (prefixSize == 2 && report[1] != reportId))
            throw new InvalidOperationException("unexpected HID feature report ID or size");
        return report.AsSpan(prefixSize, payloadSize).ToArray();
    }
}
