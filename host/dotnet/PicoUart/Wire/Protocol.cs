using System.Buffers.Binary;
using System.Text.Json;

namespace PicoUart.Wire;

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
    ushort CdcRxFifoPeakBytes);
public sealed record ActionResult(byte Action, byte Status, byte ErrorReason);

public static class Protocol
{
    public const int ChannelCount = 6;
    public const byte LayoutVersion = 1;
    public const ushort ProductId = 1;
    public const byte IdentityReportId = 1;
    public const int IdentitySize = 27;
    public const byte ConfigurationReportId = 2;
    public const int ConfigurationSize = 36;
    public const byte TelemetryReportId = 3;
    public const int TelemetrySize = 20;
    public const byte ChannelReportId = 10;
    public const int ChannelSize = 30;
    public const byte ActionReportId = 20;
    public const int ActionSize = 4;
    public const byte DiagnosticsReportId = 30;
    public const int DiagnosticsSize = 24;

    public static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower
    };

    public static IdentityPrefix ParseIdentityPrefix(ReadOnlySpan<byte> prefix)
    {
        Require(prefix.Length >= 3, "HID identity discovery prefix is truncated");
        return new(prefix[0], U16(prefix, 1));
    }

    public static BoardIdentity ParseIdentity(ReadOnlySpan<byte> payload)
    {
        Layout(payload, IdentitySize);
        var prefix = ParseIdentityPrefix(payload);
        Require(prefix.ProductId == ProductId, $"unsupported HID product (0x{prefix.ProductId:X4})");
        var total = U32(payload, 19);
        var reserved = U32(payload, 23);
        Require((payload[17] & ~7) == 0 && (payload[18] & ~31) == 0 && (payload[18] & 1) != 0,
            "invalid capabilities or clock profile mask");
        Require(total != 0 && reserved <= total, "invalid firmware RAM accounting");
        return new(prefix.ProductId, "PicoUart", Convert.ToHexString(payload.Slice(3, 8)),
            payload[11], payload[12], payload[13], payload[14], payload[15], payload[16],
            payload[17], payload[18], total, reserved);
    }

    public static ConfigurationStatus ParseConfiguration(ReadOnlySpan<byte> payload, BoardIdentity identity)
    {
        Layout(payload, ConfigurationSize);
        var stored = payload[1];
        var count = payload[2];
        Require(count == ChannelCount, "unsupported PicoUart channel count");
        Require(stored == byte.MaxValue || IsProfileSupported(identity, stored), "invalid stored clock profile");
        Require((identity.HardwareChannelMask & ~((1 << count) - 1)) == 0,
            "hardware channel mask exceeds the channel count");
        var pins = new ChannelPins[count];
        for (var channel = 0; channel < count; channel++)
        {
            var offset = 3 + channel * 4;
            pins[channel] = new(Pin(payload[offset]), Pin(payload[offset + 1]),
                Pin(payload[offset + 2]), Pin(payload[offset + 3]));
        }
        var txCapacity = U16(payload, 28);
        var rxCapacity = U16(payload, 30);
        var cdcTxCapacity = U16(payload, 32);
        var cdcRxCapacity = U16(payload, 34);
        Require(txCapacity != 0 && rxCapacity != 0 && cdcTxCapacity != 0 && cdcRxCapacity != 0,
            "invalid shared buffer capacities");
        return new(stored == byte.MaxValue ? null : stored, count, pins, payload[27],
            txCapacity, rxCapacity, cdcTxCapacity, cdcRxCapacity);
    }

    public static Telemetry ParseTelemetry(ReadOnlySpan<byte> payload, BoardIdentity identity)
    {
        Layout(payload, TelemetrySize);
        Require((payload[1] & ~1) == 0 && ((identity.Capabilities & 1) != 0 || payload[1] == 0),
            "invalid LED flags");
        var loads = new ushort?[2];
        for (var core = 0; core < loads.Length; core++)
        {
            var value = U16(payload, 12 + core * 2);
            Require(value <= 10000 || value == ushort.MaxValue, "invalid service load");
            loads[core] = value == ushort.MaxValue ? null : value;
        }
        var clock = U32(payload, 16);
        Require(clock != 0, "invalid system clock");
        bool? on = (identity.Capabilities & 1) != 0 ? payload[1] != 0 : null;
        return new(new(on), BinaryPrimitives.ReadInt16LittleEndian(payload[2..]) / 100.0,
            new(BinaryPrimitives.ReadUInt64LittleEndian(payload[4..]), loads), clock);
    }

    public static ChannelStatus ParseChannel(byte reportId, ReadOnlySpan<byte> payload,
        BoardIdentity identity, ConfigurationStatus configuration)
    {
        Require(reportId >= ChannelReportId && reportId < ChannelReportId + ChannelCount,
            "invalid channel report ID");
        Layout(payload, ChannelSize);
        var id = reportId - ChannelReportId;
        var state = payload[1];
        var measurement = payload[2];
        var baud = U32(payload, 3);
        var dataBits = payload[7];
        var stopBits = payload[8];
        var parity = payload[9];
        Require(state <= 4 && measurement <= 2, "invalid channel or UART measurement state");
        Require(baud == 0
                ? dataBits == 0 && stopBits == 0 && parity == 0
                : baud is >= 50 and <= 3_000_000 && dataBits is >= 5 and <= 8
                    && stopBits is 1 or 2 && parity <= 2,
            "invalid applied UART framing");
        if (baud != 0 && (identity.HardwareChannelMask & (1 << id)) == 0)
            Require(dataBits == 8 && stopBits == 1 && parity == 0, "invalid PIO UART framing");
        string[] states = ["closed", "starting", "ready", "configuring", "error"];
        return new(id, states[state],
            measurement, baud, dataBits, stopBits, baud == 0 ? null : new[] { "none", "odd", "even" }[parity],
            U16(payload, 10), U16(payload, 12), U32(payload, 14), U32(payload, 18),
            U32(payload, 22), U32(payload, 26));
    }

    public static ChannelDiagnostics ParseDiagnostics(byte reportId, ReadOnlySpan<byte> payload,
        ConfigurationStatus configuration)
    {
        Require(reportId >= DiagnosticsReportId && reportId < DiagnosticsReportId + ChannelCount,
            "invalid channel diagnostics report ID");
        Layout(payload, DiagnosticsSize);
        var measurement = payload[1];
        var age = payload[2];
        var error = payload[3];
        var overrun = U32(payload, 4);
        var rxError = U32(payload, 8);
        var lastFault = U32(payload, 12);
        var uartTxPeak = U16(payload, 16);
        var uartRxPeak = U16(payload, 18);
        var cdcTxPeak = U16(payload, 20);
        var cdcRxPeak = U16(payload, 22);
        Require(measurement <= 2 && error <= 5, "invalid diagnostic enum");
        Require(measurement != 0 || (age == byte.MaxValue && overrun == 0 && rxError == 0
            && uartTxPeak == 0 && uartRxPeak == 0), "invalid unavailable UART diagnostics");
        Require(measurement != 1 || age == 0, "current UART diagnostics must have zero age");
        Require(overrun != 0 || rxError != 0 || error != 0 || lastFault == 0,
            "invalid shared diagnostic timestamp");
        Require(uartTxPeak <= configuration.UartTxRingCapacityBytes
            && uartRxPeak <= configuration.UartRxRingCapacityBytes
            && cdcTxPeak <= configuration.CdcTxFifoCapacityBytes
            && cdcRxPeak <= configuration.CdcRxFifoCapacityBytes,
            "diagnostic peak exceeds advertised capacity");
        return new(reportId - DiagnosticsReportId, measurement, age, error, overrun, rxError,
            lastFault, uartTxPeak, uartRxPeak, cdcTxPeak, cdcRxPeak);
    }

    public static ActionResult ParseActionResult(ReadOnlySpan<byte> payload)
    {
        Layout(payload, ActionSize);
        var action = payload[1];
        var status = payload[2];
        var error = payload[3];
        Require(status <= 6, "invalid board action status");
        Require(status switch
        {
            0 => action == 0 && error == 0,
            1 or 2 => action is >= 1 and <= 4 && error == 0,
            3 => error is >= 1 and <= 4 && (action == 0 || action is >= 1 and <= 4),
            4 => error == 5 && (action == 0 || action is >= 1 and <= 4),
            5 => action == 4 && error == 6,
            6 => action == 4 && error is >= 7 and <= 9,
            _ => false
        }, "inconsistent board action status and error reason");
        Require(error is not (2 or 3) || action == 4, "profile error for a non-save action");
        Require(error != 4 || action is 3 or 4, "confirmation error for a non-destructive action");
        return new(action, status, error);
    }

    public static byte[] BuildAction(byte action, BoardIdentity identity, uint profile = 0,
        bool unsafeOverclock = false)
    {
        var capability = action switch
        {
            1 or 2 => 1,
            3 => 2,
            4 => 4,
            _ => throw new ArgumentOutOfRangeException(nameof(action), "unknown board action")
        };
        Require((identity.Capabilities & capability) != 0, "board action unavailable");
        byte flags;
        switch (action)
        {
            case 1 or 2:
                if (profile != 0 || unsafeOverclock)
                    throw new ArgumentException("LED actions do not accept arguments");
                flags = 0;
                break;
            case 3:
                if (profile != 0 || unsafeOverclock)
                    throw new ArgumentException("reboot action does not accept arguments");
                flags = 1;
                break;
            default:
                if (!IsProfileSupported(identity, profile))
                    throw new ArgumentOutOfRangeException(nameof(profile), "clock profile is not advertised by this board");
                if (profile != 0 && !unsafeOverclock)
                    throw new ArgumentException("explicit unsafe overclock acknowledgement is required",
                        nameof(unsafeOverclock));
                flags = profile == 0 ? (byte)1 : (byte)3;
                break;
        }
        return [LayoutVersion, action, flags, checked((byte)profile)];
    }

    public static bool IsProfileSupported(BoardIdentity identity, uint profile) =>
        profile < 5 && (identity.ClockProfileMask & (1 << (int)profile)) != 0;

    public static string ActionStatusName(byte status) => status switch
    {
        0 => "idle",
        1 => "pending",
        2 => "completed",
        3 => "invalid",
        4 => "unsupported",
        5 => "busy",
        6 => "failed",
        _ => "unknown"
    };

    private static byte? Pin(byte pin)
    {
        Require(pin <= 47 || pin == byte.MaxValue, "invalid UART GPIO");
        return pin == byte.MaxValue ? null : pin;
    }

    private static ushort U16(ReadOnlySpan<byte> data, int offset) =>
        BinaryPrimitives.ReadUInt16LittleEndian(data[offset..]);
    private static uint U32(ReadOnlySpan<byte> data, int offset) =>
        BinaryPrimitives.ReadUInt32LittleEndian(data[offset..]);
    private static void Layout(ReadOnlySpan<byte> payload, int size) =>
        Require(payload.Length == size && payload[0] == LayoutVersion,
            "unsupported report size or layout version");
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
