// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using HidSharp;
using HidSharp.Reports;
using PicoUart.Protocol;

namespace PicoUart.Hid;

/// <summary>Adapt native HID descriptors and Feature framing to protocol payloads.</summary>
internal static class Reports
{
    /// <summary>Report size including ID, excluding trailing constant padding reconstructed by Windows.</summary>
    internal static int ReportDataLength(Report report)
    {
        var bits = 0;
        var padding = false;
        foreach (var item in report.DataItems)
        {
            if (item.IsConstant)
            {
                padding = true;
                continue;
            }
            if (padding || item.TotalBits % 8 != 0)
                throw new InvalidOperationException("unsupported HID report data alignment or internal padding");
            bits = checked(bits + item.TotalBits);
        }
        return bits / 8 + 1;
    }

    internal static void ValidateReports(IEnumerable<(byte Id, int Length)> features,
        IEnumerable<(byte Id, int Length)> inputs)
    {
        (byte Id, int Length)[] expectedFeatures =
        [
            (Codec.IdentityReportId, Codec.IdentitySize + 1),
            (Codec.ConfigurationReportId, Codec.ConfigurationSize + 1),
            (Codec.TelemetryReportId, Codec.TelemetrySize + 1),
            (Codec.ActionReportId, Codec.ActionSize + 1),
            .. Enumerable.Range(0, Codec.ChannelCount)
                .Select(index => ((byte)(Codec.ChannelReportId + index), Codec.ChannelSize + 1)),
            .. Enumerable.Range(0, Codec.ChannelCount)
                .Select(index => ((byte)(Codec.DiagnosticsReportId + index), Codec.DiagnosticsSize + 1))
        ];
        var expectedInputs = Enumerable.Range(0, Codec.ChannelCount)
            .Select(index => ((byte)(Codec.ChannelReportId + index), Codec.ChannelSize + 1));
        if (!features.OrderBy(report => report.Item1).SequenceEqual(expectedFeatures.OrderBy(report => report.Item1))
            || !inputs.OrderBy(report => report.Item1).SequenceEqual(expectedInputs.OrderBy(report => report.Item1)))
            throw new InvalidOperationException("unsupported HID report descriptor; consolidated v1 reports required");
    }

    internal static byte[] ReadFeaturePayload(HidStream target, StreamLifetime lifetime,
        byte reportId, int payloadSize, int timeoutMs = 3000)
    {
        var prefixSize = OperatingSystem.IsLinux() ? 2 : 1;
        var report = new byte[Math.Max(payloadSize + prefixSize,
            target.Device.GetMaxFeatureReportLength() + prefixSize - 1)];
        report[0] = reportId;
        lifetime.Transfer(() => { target.GetFeature(report); return true; }, timeoutMs);
        return FeaturePayload(report, reportId, payloadSize, prefixSize);
    }

    internal static byte[] FeaturePayload(byte[] report, byte reportId, int payloadSize, int prefixSize)
    {
        if (report.Length < payloadSize + prefixSize || report[0] != reportId
            || (prefixSize == 2 && report[1] != reportId))
            throw new InvalidOperationException("unexpected HID feature report ID or size");
        return report.AsSpan(prefixSize, payloadSize).ToArray();
    }
}
