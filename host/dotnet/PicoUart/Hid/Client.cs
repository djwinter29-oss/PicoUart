// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using HidSharp;
using System.Diagnostics;
using PicoUart.Protocol;

namespace PicoUart.Hid;

public sealed partial class Client : IDisposable, IAsyncDisposable
{
    private readonly HidStream stream;
    private readonly StreamLifetime streamLifetime;
    private readonly string devicePath;
    private readonly object gate = new();
    private BoardIdentity? identity;
    private ConfigurationStatus? configuration;
    private bool disposed;
    private bool transferUnconfirmed;

    internal Client(HidStream stream, BoardIdentity identity)
    {
        this.stream = stream;
        this.identity = identity;
        devicePath = stream.Device.DevicePath;
        streamLifetime = new(stream.Dispose, devicePath);
        stream.ReadTimeout = 250;
        stream.WriteTimeout = 3000;
    }

    public Client(string? serialNumber = null, string? devicePath = null)
    {
        var device = Discovery.SelectDevice(serialNumber, devicePath);
        StreamLifetime.EnsureDeviceCleanupComplete(device.DevicePath);
        this.devicePath = device.DevicePath;
        var descriptor = device.GetReportDescriptor();
        var identityReports = descriptor.DeviceItems.SelectMany(item => item.FeatureReports)
            .Where(report => report.ReportID == Codec.IdentityReportId).ToArray();
        if (identityReports.Length != 1 || Reports.ReportDataLength(identityReports[0]) < 4)
            throw new InvalidOperationException("HID identity report is missing or has a truncated discovery prefix");
        stream = device.Open();
        streamLifetime = new(stream.Dispose, device.DevicePath);
        try
        {
            stream.ReadTimeout = 250;
            stream.WriteTimeout = 3000;
            var payload = Reports.ReadFeaturePayload(stream, streamLifetime,
                Codec.IdentityReportId, Reports.ReportDataLength(identityReports[0]) - 1);
            var prefix = Codec.ParseIdentityPrefix(payload);
            if (prefix.ProductId != Codec.ProductId)
                throw new InvalidOperationException($"unsupported HID product (0x{prefix.ProductId:X4})");
            identity = Codec.ParseIdentity(payload);
            Discovery.ValidateUsbSerial(device.GetSerialNumber(), identity.DeviceUid);
            Reports.ValidateReports(descriptor.DeviceItems.SelectMany(item => item.FeatureReports)
                    .Select(report => (report.ReportID, Reports.ReportDataLength(report))),
                descriptor.DeviceItems.SelectMany(item => item.InputReports)
                    .Select(report => (report.ReportID, Reports.ReportDataLength(report))));
        }
        catch
        {
            streamLifetime.TrackDeviceCleanup(device.DevicePath);
            streamLifetime.Dispose();
            throw;
        }
    }

    public static HidBoard[] Discover() => Discovery.Discover();

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
            stream.ReadTimeout = Math.Min(timeoutMs, remainingMs);
            var report = new byte[stream.Device.GetMaxInputReportLength()];
            int count;
            try { count = stream.Read(report, 0, report.Length); }
            catch (TimeoutException) { return null; }
            if (count != Codec.ChannelSize + 1)
                throw new InvalidOperationException("unexpected HID channel report size");
            return Codec.ParseChannel(report[0], report.AsSpan(1, count - 1), info);
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
            return identity ??= Codec.ParseIdentity(
                ReadFeature(Codec.IdentityReportId, Codec.IdentitySize, timeoutMs));
        }
    }

    public ConfigurationStatus ReadConfiguration() => ReadConfiguration(3000);

    private ConfigurationStatus ReadConfiguration(int timeoutMs)
    {
        lock (gate)
        {
            var info = ReadIdentity(timeoutMs);
            return configuration ??= Codec.ParseConfiguration(
                ReadFeature(Codec.ConfigurationReportId, Codec.ConfigurationSize, timeoutMs), info);
        }
    }

    public Telemetry ReadTelemetry()
    {
        lock (gate)
        {
            var info = ReadIdentity();
            return Codec.ParseTelemetry(ReadFeature(Codec.TelemetryReportId, Codec.TelemetrySize), info);
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
    public uint?[] ReadOverflowCounts() => ReadChannelDiagnostics()
        .Select(channel => channel.KnownRxOverrunCount).ToArray();

    /// <summary>Non-consuming Feature sample; never add its traffic to interrupt totals.</summary>
    public ChannelStatus ReadChannel(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index, Codec.ChannelCount);
        lock (gate)
        {
            var id = (byte)(Codec.ChannelReportId + index);
            return Codec.ParseChannel(id, ReadFeature(id, Codec.ChannelSize),
                ReadIdentity());
        }
    }

    public ChannelDiagnostics ReadChannelDiagnostic(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(index, Codec.ChannelCount);
        lock (gate)
        {
            var id = (byte)(Codec.DiagnosticsReportId + index);
            return Codec.ParseDiagnostics(id, ReadFeature(id, Codec.DiagnosticsSize), ReadConfiguration());
        }
    }

    public ChannelDiagnostics[] ReadChannelDiagnostics() =>
        Enumerable.Range(0, Codec.ChannelCount).Select(ReadChannelDiagnostic).ToArray();

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
            try { return Reports.ReadFeaturePayload(stream, streamLifetime, reportId, payloadSize, timeoutMs); }
            catch (TimeoutException)
            {
                transferUnconfirmed = true;
                throw;
            }
        }
    }

    private ActionResult ReadActionResultCore(int timeoutMs) =>
        Codec.ParseActionResult(ReadFeature(Codec.ActionReportId, Codec.ActionSize, timeoutMs));

    private static int RemainingTimeoutMs(Stopwatch timer, int timeoutMs) =>
        Math.Max(0, (int)(timeoutMs - timer.Elapsed.TotalMilliseconds));
}
