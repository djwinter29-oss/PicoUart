using System.Buffers.Binary;
using System.Text.Json;

namespace PicoUart;

public sealed record BoardStatus(double TemperatureCelsius, string FirmwareVersion,
    byte FirmwareMajor, byte FirmwareMinor, byte FirmwarePatch, bool HidResetEnabled);

public sealed record HardwareInfo(string Mcu, uint SystemClockHz, byte McuId = 0);

public sealed record ChannelStatus(int Id, byte Health, int RingHighWatermark,
    ushort ControllerTxBytes, ushort ControllerRxBytes, ushort CdcTxBytes, ushort CdcRxBytes);

public sealed record Status(byte Sequence, ChannelStatus[] Channels);

public static class Protocol
{
    public const int ChannelCount = 6;
    public const byte LayoutVersion = 15;
    public const byte HardwareInfoReportId = 6;
    public const int HardwareInfoSize = 6;
    public const byte HardwareInfoLayoutVersion = 1;
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