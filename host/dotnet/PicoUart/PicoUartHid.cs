using HidSharp;

namespace PicoUart;

public sealed class PicoUartHid : IDisposable
{
    private readonly HidStream stream;
    private readonly object gate = new();
    private bool disposed;

    public PicoUartHid(string? serialNumber = null, string? devicePath = null)
    {
        if (serialNumber is not null && devicePath is not null)
            throw new ArgumentException("--serial and --device-path cannot be used together");
        var devices = DeviceList.Local.GetHidDevices(0xCAFE, 0x4010)
            .Where(device => devicePath is null || device.DevicePath == devicePath)
            .Where(device => serialNumber is null || device.GetSerialNumber() == serialNumber)
            .Where(device => device.GetReportDescriptor().DeviceItems
                .Any(item => item.Usages.GetAllValues().Contains(0xFF000001u))).ToArray();
        if (devices.Length == 0)
            throw new InvalidOperationException("PicoUart HID interface not found with the expected usage metadata");
        if (devices.Length != 1)
            throw new InvalidOperationException("multiple PicoUart HID interfaces matched; use --serial or --device-path");
        stream = devices[0].Open();
        stream.ReadTimeout = 250;
        stream.WriteTimeout = 1000;
    }

    public Status? ReadStatus(int timeoutMs = 250)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(timeoutMs);
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            stream.ReadTimeout = timeoutMs;
            var report = new byte[stream.Device.GetMaxInputReportLength()];
            int count;
            try { count = stream.Read(report, 0, report.Length); }
            catch (TimeoutException) { return null; }
            if (count != 64 || report[0] != 1)
                throw new InvalidOperationException("unexpected HID status report ID or size");
            return Protocol.ParseStatus(report.AsSpan(1, count - 1));
        }
    }

    public BoardStatus ReadBoardStatus() => Protocol.ParseBoardStatus(ReadFeature(3, 8));

    public uint[] ReadOverflowCounts() => Protocol.ParseOverflowCounts(ReadFeature(5, 25));

    public HardwareInfo ReadHardwareInfo() => Protocol.ParseHardwareInfo(
        ReadFeature(Protocol.HardwareInfoReportId, Protocol.HardwareInfoSize));

    public void ToggleLed()
    {
        lock (gate) SendCommand(1);
    }

    public void ResetBoard()
    {
        lock (gate)
        {
            if (!ReadBoardStatus().HidResetEnabled)
                throw new InvalidOperationException("firmware HID reset is disabled; rebuild with -DPICO_UART_ALLOW_HID_RESET=1");
            SendCommand(3);
            Thread.Sleep(50);
            SendCommand(2);
        }
    }

    public void Dispose()
    {
        lock (gate)
        {
            if (disposed) return;
            disposed = true;
            stream.Dispose();
        }
    }

    private byte[] ReadFeature(byte reportId, int payloadSize)
    {
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            var prefixSize = OperatingSystem.IsLinux() ? 2 : 1;
            var report = new byte[Math.Max(payloadSize + prefixSize,
                stream.Device.GetMaxFeatureReportLength() + prefixSize - 1)];
            report[0] = reportId;
            stream.GetFeature(report);
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

    private void SendCommand(byte command)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        var report = new byte[Math.Max(2, stream.Device.GetMaxFeatureReportLength())];
        report[0] = 4;
        report[1] = command;
        stream.SetFeature(report);
    }
}