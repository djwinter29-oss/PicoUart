// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Buffers.Binary;
using System.Collections.Concurrent;
using HidSharp;
using PicoUart.Hid;

namespace PicoUart.Tests.Hid;

internal sealed class ScriptedHidStream() : HidStream(new ScriptedHidDevice())
{
    public Func<byte, byte[]>? FeatureRead { get; set; }
    public Action<byte[]>? FeatureWritten { get; set; }
    public Func<byte[]>? InputRead { get; set; }
    public ConcurrentQueue<byte[]> Writes { get; } = new();
    public ActionResult Result { get; set; } = new(0, 0, 0);
    public override int ReadTimeout { get; set; } = 250;
    public override int WriteTimeout { get; set; } = 3000;

    public Client CreateClient(BoardIdentity? identity = null) => new(this, identity ?? ProtocolFixtures.Identity());

    public byte[] DefaultFeature(byte id)
    {
        if (id == Codec.IdentityReportId) return ProtocolFixtures.IdentityPayload();
        if (id == Codec.ConfigurationReportId) return ProtocolFixtures.ConfigurationPayload();
        if (id == Codec.ActionReportId) return [1, Result.Action, Result.Status, Result.ErrorReason];
        if (id == Codec.TelemetryReportId)
        {
            var telemetry = new byte[Codec.TelemetrySize];
            telemetry[0] = 1;
            BinaryPrimitives.WriteUInt32LittleEndian(telemetry.AsSpan(16), 150_000_000);
            return telemetry;
        }
        if (id >= Codec.ChannelReportId && id < Codec.ChannelReportId + Codec.ChannelCount)
        {
            var channel = new byte[Codec.ChannelSize];
            channel[0] = 1;
            channel[2] = 1;
            BinaryPrimitives.WriteUInt16LittleEndian(channel.AsSpan(12), 500);
            return channel;
        }
        if (id >= Codec.DiagnosticsReportId && id < Codec.DiagnosticsReportId + Codec.ChannelCount)
        {
            var diagnostics = new byte[Codec.DiagnosticsSize];
            diagnostics[0] = 1;
            diagnostics[1] = 1;
            return diagnostics;
        }
        throw new InvalidOperationException($"unexpected simulated report {id}");
    }

    public override void GetFeature(byte[] buffer, int offset, int count)
    {
        var id = buffer[offset];
        var payload = FeatureRead is null ? DefaultFeature(id) : FeatureRead(id);
        var prefixSize = OperatingSystem.IsLinux() ? 2 : 1;
        if (prefixSize == 2) buffer[offset + 1] = id;
        payload.CopyTo(buffer, offset + prefixSize);
    }

    public override void SetFeature(byte[] buffer, int offset, int count)
    {
        var report = buffer.AsSpan(offset, count).ToArray();
        Writes.Enqueue(report);
        Result = new(report[2], 2, 0);
        FeatureWritten?.Invoke(report);
    }

    public override int Read(byte[] buffer, int offset, int count)
    {
        if (InputRead is null)
        {
            Thread.Sleep(ReadTimeout);
            throw new TimeoutException("no simulated Input report");
        }
        var report = InputRead();
        report.CopyTo(buffer, offset);
        return report.Length;
    }

    public override void Write(byte[] buffer, int offset, int count) =>
        throw new NotSupportedException("PicoUart has no Output reports");

    private sealed class ScriptedHidDevice : HidDevice
    {
        public override int ProductID => 0x116B;
        public override int ReleaseNumberBcd => 0x0100;
        public override int VendorID => 0x2E8A;
        public override string DevicePath { get; } = $"simulated-{Guid.NewGuid():N}";
        public override int GetMaxFeatureReportLength() => Codec.ConfigurationSize + 1;
        public override int GetMaxInputReportLength() => Codec.ChannelSize + 1;
        public override int GetMaxOutputReportLength() => 0;
        public override string GetFileSystemName() => "simulated";
        protected override DeviceStream OpenDeviceDirectly(OpenConfiguration configuration) =>
            throw new InvalidOperationException("tests must not open physical devices");
    }
}
