using System.Buffers.Binary;
using System.Text;
using System.Text.Json;

namespace PicoUart.Wire;

public sealed record BoardStatus(double TemperatureCelsius, string FirmwareVersion,
    byte FirmwareMajor, byte FirmwareMinor, byte FirmwarePatch, bool HidResetEnabled);
public sealed record HardwareInfo(string Mcu, uint SystemClockHz, byte McuId);
public sealed record ConfigurationStatus(string Source, string Reason, uint? StoredProfile, uint ActiveProfile);
public sealed record ConfigurationSaveStatus(byte Status, uint? Profile);
public sealed record BoardIdentity(string ProductName, string SerialNumber, byte FirmwareMajor, byte FirmwareMinor,
    byte FirmwarePatch, byte McuId, byte HardwareChannelMask, byte PioChannelMask, ushort Capabilities,
    byte ClockProfileMask, uint RamTotalBytes)
{
    public string FirmwareVersion => $"{FirmwareMajor}.{FirmwareMinor}.{FirmwarePatch}";
    public string Mcu => McuId switch { 1 => "RP2040", 2 => "RP2350", _ => $"Unknown MCU ({McuId})" };
}
public sealed record LedStatus(bool? ManualOn, bool? OutputOn);
public sealed record RuntimeStatus(ulong UptimeMs, ushort?[] CpuUsageBasisPoints,
    uint RamReservedBytes, uint RamTotalBytes);
public sealed record Telemetry(LedStatus Led, double TemperatureCelsius, RuntimeStatus Runtime, uint SystemClockHz);
public sealed record FaultHistory(uint Count);
public sealed record ChannelDiagnostics(byte Health, uint BaudRate, byte DataBits, byte StopBits,
    string? Parity, byte? TxPin, byte? RxPin, Dictionary<string, FaultHistory> Faults, uint LastFaultUptimeS);
public sealed record ChannelStatus(int Id, ushort Sequence, uint IntervalMs, byte Health, int RingHighWatermark,
    uint UartTxBytes, uint UartRxBytes, uint CdcTxBytes, uint CdcRxBytes, ChannelDiagnostics Diagnostics);
public sealed record ControlResult(byte Command, byte Flags, byte Status, uint RequestId, uint? RequestedProfile,
    ConfigurationStatus Configuration, byte ErrorReason);

public static class Protocol
{
    public const int ChannelCount = 6;
    public const byte LayoutVersion = 1;
    public const byte IdentityReportId = 1;
    public const int IdentitySize = 63;
    public const byte ControlReportId = 2;
    public const int ControlSize = 24;
    public const byte TelemetryReportId = 3;
    public const int TelemetrySize = 24;
    public const byte ChannelReportId = 10;
    public const int ChannelSize = 54;
    public static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower
    };

    public static BoardIdentity ParseIdentity(ReadOnlySpan<byte> payload)
    {
        Layout(payload, IdentitySize);
        var product = payload.Slice(1, 32);
        var end = product.IndexOf((byte)0);
        Require(end > 0, "invalid HID product identity");
        for (var index = 0; index < product.Length; index++)
            Require(index < end ? product[index] is >= 32 and <= 126 : product[index] == 0,
                "invalid HID product identity");
        var serial = Encoding.ASCII.GetString(payload.Slice(33, 16));
        Require(serial.All(char.IsAsciiHexDigit), "invalid HID serial identity");
        var hw = payload[54];
        var pio = payload[55];
        var capabilities = U16(payload, 56);
        Require(payload[53] == ChannelCount && (hw & pio) == 0 && (hw | pio) == 63,
            "invalid channel count or backend masks");
        Require((capabilities & ~7) == 0 && (payload[58] & ~31) == 0 && (payload[58] & 1) != 0,
            "invalid capabilities or clock profile mask");
        var total = U32(payload, 59);
        Require(total != 0, "invalid total RAM");
        return new(Encoding.ASCII.GetString(product[..end]), serial, payload[49], payload[50], payload[51],
            payload[52], hw, pio, capabilities, payload[58], total);
    }

    public static Telemetry ParseTelemetry(ReadOnlySpan<byte> payload, BoardIdentity identity)
    {
        Layout(payload, TelemetrySize);
        Require((payload[1] & ~1) == 0 && ((identity.Capabilities & 1) != 0 || payload[1] == 0),
            "invalid LED flags");
        var available = (identity.Capabilities & 1) != 0;
        bool? on = available ? payload[1] != 0 : null;
        ushort?[] cpu = new ushort?[2];
        for (var core = 0; core < cpu.Length; core++)
        {
            var value = U16(payload, 12 + core * 2);
            Require(value <= 10000 || value == ushort.MaxValue, "invalid runtime CPU value");
            cpu[core] = value == ushort.MaxValue ? null : value;
        }
        var reserved = U32(payload, 16);
        var clock = U32(payload, 20);
        Require(reserved <= identity.RamTotalBytes && clock != 0, "invalid runtime RAM or clock");
        return new(new(on, on), BinaryPrimitives.ReadInt16LittleEndian(payload[2..]) / 100.0,
            new(BinaryPrimitives.ReadUInt64LittleEndian(payload[4..]), cpu, reserved, identity.RamTotalBytes), clock);
    }

    public static ChannelStatus ParseChannel(byte reportId, ReadOnlySpan<byte> payload)
    {
        Require(reportId >= ChannelReportId && reportId < ChannelReportId + ChannelCount, "invalid channel report ID");
        Layout(payload, ChannelSize);
        var baud = U32(payload, 25);
        Require(baud == 0
                ? payload[29] == 0 && payload[30] == 0 && payload[31] == 0
                : payload[29] is >= 5 and <= 8 && payload[30] is 1 or 2 && payload[31] <= 2,
            "invalid applied UART framing");
        Require((payload[32] <= 47 || payload[32] == 255) && (payload[33] <= 47 || payload[33] == 255),
            "invalid UART GPIO");
        string[] labels = ["init_failed", "control_error", "rx_overrun", "rx_error"];
        var faults = new Dictionary<string, FaultHistory>();
        for (var index = 0; index < labels.Length; index++)
            faults[labels[index]] = new(U32(payload, 34 + index * 4));
        var last = U32(payload, 50);
        Require(faults.Values.Any(value => value.Count != 0) || last == 0, "invalid shared fault timestamp");
        Require((faults["rx_overrun"].Count == 0) == ((payload[7] & 64) == 0)
            && (faults["rx_error"].Count == 0) == ((payload[7] & 128) == 0), "inconsistent historical RX flags");
        var diagnostics = new ChannelDiagnostics(payload[7], baud, payload[29], payload[30],
            baud == 0 ? null : "NOE"[payload[31]].ToString(), payload[32] == 255 ? null : payload[32],
            payload[33] == 255 ? null : payload[33], faults, last);
        return new(reportId - ChannelReportId, U16(payload, 1), U32(payload, 3), payload[7], payload[8] * 16,
            U32(payload, 9), U32(payload, 13), U32(payload, 17), U32(payload, 21), diagnostics);
    }

    public static ControlResult ParseControl(ReadOnlySpan<byte> payload, BoardIdentity identity)
    {
        Layout(payload, ControlSize);
        var command = payload[1];
        var flags = payload[2];
        var status = payload[3];
        var id = U32(payload, 4);
        var stored = U32(payload, 8);
        var active = U32(payload, 12);
        var requested = U32(payload, 16);
        var source = payload[20];
        var reason = payload[21];
        var error = payload[22];
        Require(payload[23] == 0 && source <= 2 && reason <= 7 && status <= 6, "invalid control metadata");
        bool Supported(uint profile) => profile < 5 && (identity.ClockProfileMask & (1 << (int)profile)) != 0;
        Require((stored == uint.MaxValue || Supported(stored)) && Supported(active), "invalid boot profile");
        Require(source != 1 || (reason == 0 && stored == active), "inconsistent stored configuration");
        Require(source != 2 || reason != 0, "inconsistent recovery configuration");
        Require(status switch
        {
            0 or 1 or 2 => error == 0,
            3 => error is >= 1 and <= 4,
            4 => error == 5,
            5 => error == 6,
            6 => error is >= 7 and <= 9,
            _ => false
        }, "inconsistent control status and error reason");
        var malformed = status == 3 && error == 1;
        var unsupported = status == 4 && error == 5;
        Require((flags & ~1) == 0 || malformed, "invalid echoed flag bits");
        Require(command is >= 1 and <= 5 || malformed || unsupported || status == 0, "invalid echoed command");
        Require(command == 5 || flags == 0 || malformed || unsupported, "invalid command flags");
        if (id == 0)
            Require((status == 0 || malformed) && command == 0 && flags == 0 && requested == uint.MaxValue,
                "invalid unidentifiable control result");
        else Require(status != 0, "idle result has a request ID");
        if (status == 0)
            Require(command == 0 && flags == 0 && requested == uint.MaxValue, "invalid idle control result");
        Require(requested == uint.MaxValue || (command == 5 && Supported(requested)
            && !malformed && !unsupported && error != 2), "invalid requested profile echo");
        Require(error is not (2 or 3) || command == 5, "profile error for a non-save command");
        Require(error != 3 || (requested is > 0 and < 5 && flags == 0), "invalid acknowledgement error");
        Require(error != 4 || command == 4, "authorization error for a non-reboot command");
        Require(status != 5 || command == 5, "busy result for a non-save command");
        Require(error is not (7 or 8) || command == 5, "flash error for a non-save command");
        if (command == 5 && status is 1 or 2 or 5 or 6)
            Require(Supported(requested) && (requested == 0 || flags == 1), "invalid accepted save result");
        if (status is 1 or 2 or 5 or 6)
        {
            var capability = command is 1 or 2 ? 1 : command is 3 or 4 ? 2 : 4;
            Require((identity.Capabilities & capability) != 0, "control result lacks advertised capability");
        }
        string[] sources = ["defaults", "stored", "recovery"];
        string[] reasons = ["none", "erased", "invalid_header", "crc_failed", "unsupported_version",
            "invalid_profile", "watchdog_recovery", "clock_unavailable"];
        return new(command, flags, status, id, requested == uint.MaxValue ? null : requested,
            new(sources[source], reasons[reason], stored == uint.MaxValue ? null : stored, active), error);
    }

    public static byte[] BuildCommand(byte command, uint requestId, BoardIdentity identity,
        uint profile = 0, bool unsafeOverclock = false)
    {
        if (requestId == 0 || command is < 1 or > 5) throw new ArgumentException("invalid command or request ID");
        var capability = command is 1 or 2 ? 1 : command is 3 or 4 ? 2 : 4;
        if ((identity.Capabilities & capability) == 0) throw new InvalidOperationException("board control unavailable");
        if (command != 5 && (profile != 0 || unsafeOverclock))
            throw new ArgumentException("unexpected command arguments");
        if (command == 5 && (profile > 4 || (identity.ClockProfileMask & (1 << (int)profile)) == 0))
            throw new ArgumentOutOfRangeException(nameof(profile), "clock profile is not advertised by this board");
        if (profile != 0 && !unsafeOverclock)
            throw new ArgumentException("explicit unsafe overclock acknowledgement is required", nameof(unsafeOverclock));
        var payload = new byte[ControlSize];
        payload[0] = LayoutVersion;
        payload[1] = command;
        payload[2] = unsafeOverclock ? (byte)1 : (byte)0;
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), requestId);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(16), profile);
        return payload;
    }

    public static string[] DecodeHealth(byte health)
    {
        string[] labels = ["ready", "init_failed", "control_error", "control_pending",
            "cdc_open", "pio", "rx_overrun", "rx_error"];
        return labels.Where((_, index) => (health & (1 << index)) != 0).ToArray();
    }

    private static ushort U16(ReadOnlySpan<byte> data, int offset) =>
        BinaryPrimitives.ReadUInt16LittleEndian(data[offset..]);
    private static uint U32(ReadOnlySpan<byte> data, int offset) =>
        BinaryPrimitives.ReadUInt32LittleEndian(data[offset..]);
    private static void Layout(ReadOnlySpan<byte> payload, int size) =>
        Require(payload.Length == size && payload[0] == LayoutVersion, "unsupported report size or layout version");
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
