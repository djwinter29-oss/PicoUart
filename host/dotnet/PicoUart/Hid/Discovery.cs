// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using HidSharp;

namespace PicoUart.Hid;

public sealed record HidBoard(string DevicePath, string? SerialNumber, string ProductName, string? Error = null);

/// <summary>Discover and select devices using validated PicoUart USB identity.</summary>
internal static class Discovery
{
    internal static HidDevice SelectDevice(string? serialNumber, string? devicePath)
    {
        if (serialNumber is not null && devicePath is not null)
            throw new ArgumentException("--serial and --device-path cannot be used together");
        var devices = MatchingDevices(DeviceList.Local.GetHidDevices(0x2E8A, 0x116B),
            serialNumber, devicePath, device => device.DevicePath,
            device => device.GetSerialNumber(), IsPicoUartDevice);
        if (devices.Length == 0)
            throw new InvalidOperationException("PicoUart HID interface not found with the expected usage metadata");
        if (devices.Length != 1)
            throw new InvalidOperationException(
                "multiple PicoUart HID interfaces matched; use --serial or --device-path");
        return devices[0];
    }

    internal static TDevice[] MatchingDevices<TDevice>(IEnumerable<TDevice> devices, string? serialNumber,
        string? devicePath, Func<TDevice, string> getDevicePath, Func<TDevice, string?> getSerialNumber,
        Func<TDevice, bool> isPicoUartDevice)
    {
        var matches = new List<TDevice>();
        Exception? metadataError = null;
        foreach (var device in devices)
        {
            if (devicePath is not null && getDevicePath(device) != devicePath) continue;
            if (serialNumber is not null)
            {
                string? candidateSerial;
                try { candidateSerial = getSerialNumber(device); }
                catch (Exception error) when (Errors.IsDeviceError(error))
                {
                    metadataError ??= error;
                    continue;
                }
                if (!string.Equals(candidateSerial, serialNumber, StringComparison.OrdinalIgnoreCase)) continue;
            }
            if (isPicoUartDevice(device)) matches.Add(device);
        }
        if (matches.Count == 0 && metadataError is not null) throw metadataError;
        return matches.ToArray();
    }

    internal static HidBoard[] Discover() => DiscoverDevices(
        DeviceList.Local.GetHidDevices(0x2E8A, 0x116B),
        device => IsPicoUartDevice(device) ? DiscoverDevice(device) : null,
        (device, error) => new(device.DevicePath, TryGetSerialNumber(device), "PicoUart unavailable", error.Message));

    internal static HidBoard[] DiscoverDevices<TDevice>(IEnumerable<TDevice> devices,
        Func<TDevice, HidBoard?> discoverDevice, Func<TDevice, Exception, HidBoard> discoveryFailure)
    {
        var discovered = new List<HidBoard>();
        foreach (var device in devices)
        {
            try
            {
                var board = discoverDevice(device);
                if (board is not null) discovered.Add(board);
            }
            catch (Exception error) when (Errors.IsDeviceError(error))
            {
                discovered.Add(discoveryFailure(device, error));
            }
        }
        return discovered.ToArray();
    }

    private static bool IsPicoUartDevice(HidDevice device) => device.GetReportDescriptor().DeviceItems
        .Any(item => item.Usages.GetAllValues().Contains(0xFF000001u));

    private static string? TryGetSerialNumber(HidDevice device)
    {
        try { return device.GetSerialNumber(); }
        catch (Exception error) when (Errors.IsDeviceError(error)) { return null; }
    }

    private static HidBoard DiscoverDevice(HidDevice device)
    {
        var features = device.GetReportDescriptor().DeviceItems.SelectMany(item => item.FeatureReports)
            .Where(report => report.ReportID == Codec.IdentityReportId).ToArray();
        if (features.Length != 1 || Reports.ReportDataLength(features[0]) < 4)
            throw new InvalidOperationException("HID identity report is missing or has a truncated discovery prefix");
        StreamLifetime.EnsureDeviceCleanupComplete(device.DevicePath);
        var candidate = device.Open();
        var lifetime = new StreamLifetime(candidate.Dispose, device.DevicePath);
        try
        {
            candidate.ReadTimeout = 3000;
            candidate.WriteTimeout = 3000;
            var payload = Reports.ReadFeaturePayload(candidate, lifetime,
                Codec.IdentityReportId, Reports.ReportDataLength(features[0]) - 1);
            var prefix = Codec.ParseIdentityPrefix(payload);
            var usbSerial = device.GetSerialNumber();
            if (prefix.ProductId != Codec.ProductId)
                return new(device.DevicePath, usbSerial, $"Unknown product (0x{prefix.ProductId:X4})");
            var identity = Codec.ParseIdentity(payload);
            ValidateUsbSerial(usbSerial, identity.DeviceUid);
            return CreateBoard(device.DevicePath, usbSerial, identity);
        }
        finally
        {
            lifetime.TrackDeviceCleanup(device.DevicePath);
            lifetime.Dispose();
        }
    }

    internal static void ValidateUsbSerial(string? usbSerial, string deviceUid)
    {
        if (usbSerial is not null && !string.Equals(usbSerial, deviceUid, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("HID identity UID does not match the USB serial number");
    }

    internal static HidBoard CreateBoard(string devicePath, string? usbSerial, BoardIdentity identity) =>
        new(devicePath, usbSerial, identity.ProductName);
}
