// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
namespace PicoUart.Hid;

public sealed record BoardStatus(double TemperatureCelsius, string FirmwareVersion,
    byte FirmwareMajor, byte FirmwareMinor, byte FirmwarePatch, bool HidResetEnabled);
public sealed record HardwareInfo(string Mcu, uint SystemClockHz, byte McuId);
public sealed record ConfigurationStatus(byte? StoredProfile, byte ChannelCount, ChannelPins[] ChannelPins,
    byte ResetCause, ushort UartTxRingCapacityBytes, ushort UartRxRingCapacityBytes,
    ushort CdcTxFifoCapacityBytes, ushort CdcRxFifoCapacityBytes);
public sealed record ConfigurationSaveStatus(byte Status, byte ErrorReason, uint? Profile);
public sealed record IdentityPrefix(byte Version, ushort ProductId);
public sealed record ChannelPins(byte? TxPin, byte? RxPin, byte? RtsPin, byte? CtsPin);
public sealed record BoardIdentity(ushort ProductId, string ProductName, string DeviceUid,
    byte FirmwareMajor, byte FirmwareMinor, byte FirmwarePatch, byte McuId, byte BoardModelId,
    byte HardwareChannelMask, byte Capabilities, byte ClockProfileMask, uint RamTotalBytes, uint RamReservedBytes)
{
    public string FirmwareVersion => $"{FirmwareMajor}.{FirmwareMinor}.{FirmwarePatch}";
    public string Mcu => McuId switch { 1 => "RP2040", 2 => "RP2350", _ => $"Unknown MCU ({McuId})" };
    public string BoardModel => BoardModelId switch
    {
        0 => "Unknown / unspecified",
        1 => "Pico",
        2 => "Pico W",
        3 => "Pico 2",
        4 => "Pico 2 W",
        _ => $"Unknown board ({BoardModelId})"
    };
}
public sealed record LedStatus(bool? OutputOn);
public sealed record RuntimeStatus(ulong UptimeMs, ushort?[] ServiceLoadBasisPoints);
public sealed record Telemetry(LedStatus Led, double TemperatureCelsius, RuntimeStatus Runtime, uint SystemClockHz);
public sealed record ChannelStatus(int Id, string State, byte UartMeasurementState, uint BaudRate,
    byte DataBits, byte StopBits, string? Parity, ushort Sequence, ushort IntervalMs,
    uint UartTxBytes, uint UartRxBytes, uint CdcTxBytes, uint CdcRxBytes);
public sealed record ChannelDiagnostics(int Id, byte UartMeasurementState, byte UartSampleAge100Ms,
    byte LastControlErrorReason, uint RxOverrunCount, uint RxErrorCount, uint LastFaultUptimeS,
    ushort UartTxRingPeakBytes, ushort UartRxRingPeakBytes, ushort CdcTxFifoPeakBytes,
    ushort CdcRxFifoPeakBytes)
{
    internal uint? KnownRxOverrunCount => UartMeasurementState == 0 ? null : RxOverrunCount;
}
public sealed record ActionResult(byte Action, byte Status, byte ErrorReason);
