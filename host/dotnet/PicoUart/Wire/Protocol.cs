using System.Buffers.Binary;
using System.Text.Json;

namespace PicoUart.Wire;

public sealed record BoardStatus(double TemperatureCelsius, string FirmwareVersion,
    byte FirmwareMajor, byte FirmwareMinor, byte FirmwarePatch, bool HidResetEnabled);

public sealed record HardwareInfo(string Mcu, uint SystemClockHz, byte McuId = 0);
/// <summary>Read-only persistent configuration diagnostics and selected clock profiles.</summary>
public sealed record ConfigurationStatus(string Source, string Reason, uint? StoredProfile, uint ActiveProfile);
/// <summary>Latest persistent configuration save request and its firmware completion status.</summary>
public sealed record ConfigurationSaveStatus(byte Status, uint? Profile);

public sealed record BoardIdentity(string ProductName, string SerialNumber);
public sealed record LedStatus(bool? ManualOn, bool? OutputOn);

public sealed record RuntimeStatus(ulong UptimeMs, ushort?[] CpuUsageBasisPoints,
    uint RamReservedBytes, uint RamTotalBytes);

public sealed record ChannelStatus(int Id, byte Health, int RingHighWatermark,
    ushort ControllerTxBytes, ushort ControllerRxBytes, ushort CdcTxBytes, ushort CdcRxBytes);

public sealed record Status(byte Sequence, ChannelStatus[] Channels);
public sealed record FaultHistory(uint Count, ulong? LastSeenMs);
public sealed record ChannelDiagnostics(byte Health, uint BaudRate, byte DataBits, byte StopBits,
    string? Parity, byte? TxPin, byte? RxPin, Dictionary<string, FaultHistory> Faults);

public static class Protocol
{
    public const byte ChannelDiagnosticsReportId = 10;
    public const int ChannelDiagnosticsSize = 59;

    public static ChannelDiagnostics ParseChannelDiagnostics(ReadOnlySpan<byte> payload)
    {
        Require(payload.Length == ChannelDiagnosticsSize && payload[0] == 1,
            "unsupported channel diagnostics size or version");
        var baud = BinaryPrimitives.ReadUInt32LittleEndian(payload[2..]);
        Require(baud == 0 || (payload[6] is >= 5 and <= 8 && payload[7] is 1 or 2 && payload[8] <= 2),
            "invalid applied UART framing");
        Require((payload[9] <= 47 || payload[9] == 255) && (payload[10] <= 47 || payload[10] == 255),
            "invalid UART GPIO");
        string[] labels = ["init_failed", "control_error", "rx_overrun", "rx_error"];
        var faults = new Dictionary<string, FaultHistory>();
        for (var index = 0; index < labels.Length; index++)
        {
            var offset = 11 + index * 12;
            var count = BinaryPrimitives.ReadUInt32LittleEndian(payload[offset..]);
            var seen = BinaryPrimitives.ReadUInt64LittleEndian(payload[(offset + 4)..]);
            Require(seen <= 9007199254740991ul && (count != 0 || seen == 0), "invalid channel fault timestamp");
            faults[labels[index]] = new(count, count == 0 ? null : seen);
        }
        return new(payload[1], baud, payload[6], payload[7], baud == 0 ? null : "NOE"[payload[8]].ToString(),
            payload[9] == 255 ? null : payload[9], payload[10] == 255 ? null : payload[10], faults);
    }

    public static LedStatus ParseLed(ReadOnlySpan<byte> payload)
    {
        Require(payload.Length == 2 && payload[0] == 1 && (payload[1] & ~7) == 0,
            "unsupported LED report size, version, or flags");
        var flags = payload[1];
        Require(((flags & 1) != 0 || flags == 0) && ((flags & 2) == 0 || (flags & 4) != 0),
            "inconsistent LED state");
        return new((flags & 1) != 0 ? (flags & 2) != 0 : null,
            (flags & 1) != 0 ? (flags & 4) != 0 : null);
    }

    public const int ChannelCount = 6;
    public const byte LayoutVersion = 1;
    public const byte HardwareInfoReportId = 6;
    public const int HardwareInfoSize = 6;
    public const byte HardwareInfoLayoutVersion = 1;
    public const byte ConfigurationReportId = 16;
    public const int ConfigurationSize = 12;
    public const byte ConfigurationSaveReportId = 17;
    public const int ConfigurationSaveSize = 8;
    public static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower
    };

    public static Status ParseStatus(ReadOnlySpan<byte> payload)
    {
        Require(payload.Length == 63, "unexpected status report size");
        Require(payload[0] == (byte)'P', "received a status report with an invalid signature");
        Require(payload[1] == LayoutVersion, "unsupported status report version");
        var channels = new ChannelStatus[ChannelCount];
        for (var index = 0; index < channels.Length; index++)
        {
            var source = payload.Slice(3 + index * 10, 10);
            channels[index] = new(index, source[0], source[1] * 16,
                BinaryPrimitives.ReadUInt16LittleEndian(source[2..]),
                BinaryPrimitives.ReadUInt16LittleEndian(source[4..]),
                BinaryPrimitives.ReadUInt16LittleEndian(source[6..]),
                BinaryPrimitives.ReadUInt16LittleEndian(source[8..]));
        }
        return new(payload[2], channels);
    }

    public static BoardStatus ParseBoardStatus(ReadOnlySpan<byte> payload)
    {
        Require(payload.Length == 8, "unexpected board-status report size");
        Require(payload[0] == LayoutVersion, "unsupported board-status report version");
        Require((payload[1] & ~1) == 0 && payload[7] == 0,
            "unsupported board-status report with unknown reserved fields");
        return new(BinaryPrimitives.ReadInt16LittleEndian(payload[2..]) / 100.0,
            $"{payload[4]}.{payload[5]}.{payload[6]}", payload[4], payload[5], payload[6], (payload[1] & 1) != 0);
    }

    public static HardwareInfo ParseHardwareInfo(ReadOnlySpan<byte> payload)
    {
        Require(payload.Length == HardwareInfoSize, "unexpected hardware-info report size");
        Require(payload[0] == HardwareInfoLayoutVersion, "unsupported hardware-info report version");
        var mcu = payload[1] switch
        {
            1 => "RP2040",
            2 => "RP2350",
            _ => $"Unknown MCU ({payload[1]})"
        };
        var clockHz = BinaryPrimitives.ReadUInt32LittleEndian(payload[2..]);
        Require(clockHz != 0, "invalid hardware-info system clock");
        return new(mcu, clockHz, payload[1]);
    }

    public static ConfigurationStatus ParseConfiguration(ReadOnlySpan<byte> payload)
    {
        Require(payload.Length == ConfigurationSize && payload[0] == 1,
            "unsupported configuration report size or version");
        string[] sources = ["defaults", "stored", "recovery"];
        string[] reasons = ["none", "erased", "invalid_header", "crc_failed", "unsupported_version",
            "invalid_profile", "watchdog_recovery", "clock_unavailable"];
        var source = payload[1];
        var reason = payload[2];
        var reserved = payload[3];
        var stored = BinaryPrimitives.ReadUInt32LittleEndian(payload[4..]);
        var active = BinaryPrimitives.ReadUInt32LittleEndian(payload[8..]);
        Require(source < sources.Length && reason < reasons.Length && reserved == 0,
            "invalid configuration source, reason, or reserved field");
        Require((stored <= 4 || stored == uint.MaxValue) && active <= 4, "invalid configuration profile");
        Require(source != 1 || (reason == 0 && stored <= 4 && active == stored), "inconsistent stored configuration");
        Require(source != 2 || reason != 0, "inconsistent recovery configuration");
        return new(sources[source], reasons[reason], stored == uint.MaxValue ? null : stored, active);
    }

    public static ConfigurationSaveStatus ParseConfigurationSave(ReadOnlySpan<byte> payload)
    {
        Require(payload.Length == ConfigurationSaveSize && payload[0] == 1,
            "unsupported configuration save report size or version");
        Require(payload[1] <= 5 && payload[2] == 0 && payload[3] == 0,
            "invalid configuration save status or reserved fields");
        var profile = BinaryPrimitives.ReadUInt32LittleEndian(payload[4..]);
        Require(profile <= 4 || (payload[1] is 0 or 3 && profile == uint.MaxValue),
            "invalid configuration save profile");
        return new(payload[1], profile == uint.MaxValue ? null : profile);
    }

    public static byte[] BuildConfigurationSave(uint profile, bool unsafeOverclock, byte mcuId)
    {
        if (profile > 4)
            throw new ArgumentOutOfRangeException(nameof(profile), "unknown system clock profile");
        if (mcuId is not (1 or 2))
            throw new ArgumentException("configuration save requires a known board MCU", nameof(mcuId));
        if (profile != 0 && !unsafeOverclock)
            throw new ArgumentException("explicit unsafe overclock acknowledgement is required", nameof(unsafeOverclock));
        if (profile is 1 or 2 && profile != mcuId)
            throw new ArgumentException("system clock profile does not match the board MCU", nameof(profile));
        var payload = new byte[ConfigurationSaveSize];
        payload[0] = 1;
        payload[1] = unsafeOverclock ? (byte)1 : (byte)0;
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), profile);
        return payload;
    }

    public static uint[] ParseOverflowCounts(ReadOnlySpan<byte> payload)
    {
        Require(payload.Length == 25, "unexpected overflow-count report size");
        Require(payload[0] == LayoutVersion, "unsupported overflow-count report version");
        var counts = new uint[ChannelCount];
        for (var index = 0; index < counts.Length; index++)
        {
            counts[index] = BinaryPrimitives.ReadUInt32LittleEndian(payload[(1 + index * 4)..]);
        }
        return counts;
    }

    public static BoardIdentity ParseIdentity(ReadOnlySpan<byte> payload)
    {
        Require(payload.Length == 49 && payload[0] == 1, "unsupported identity report size or version");
        var product = payload.Slice(1, 32);
        var end = product.IndexOf((byte)0);
        Require(end > 0, "invalid HID product identity");
        for (var index = 0; index < product.Length; index++)
            Require(index < end ? product[index] is >= 32 and <= 126 : product[index] == 0,
                "invalid HID product identity");
        var serial = System.Text.Encoding.ASCII.GetString(payload.Slice(33, 16));
        Require(serial.All(char.IsAsciiHexDigit), "invalid HID serial identity");
        return new(System.Text.Encoding.ASCII.GetString(product[..end]), serial);
    }

    public static RuntimeStatus ParseRuntime(ReadOnlySpan<byte> payload)
    {
        Require(payload.Length == 21 && payload[0] == 1, "unsupported runtime report size or version");
        ushort?[] cpu = new ushort?[2];
        for (var core = 0; core < cpu.Length; core++)
        {
            var value = BinaryPrimitives.ReadUInt16LittleEndian(payload[(9 + core * 2)..]);
            Require(value <= 10000 || value == ushort.MaxValue, "invalid runtime CPU value");
            cpu[core] = value == ushort.MaxValue ? null : value;
        }
        var reserved = BinaryPrimitives.ReadUInt32LittleEndian(payload[13..]);
        var total = BinaryPrimitives.ReadUInt32LittleEndian(payload[17..]);
        Require(total != 0 && reserved <= total, "invalid runtime RAM values");
        return new(BinaryPrimitives.ReadUInt64LittleEndian(payload[1..]), cpu, reserved, total);
    }

    public static string[] DecodeHealth(byte health)
    {
        string[] labels = ["ready", "init_failed", "control_error", "control_pending",
            "cdc_open", "pio", "rx_overrun", "rx_error"];
        return labels.Where((_, index) => (health & (1 << index)) != 0).ToArray();
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}