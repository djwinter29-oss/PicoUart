// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using HidSharp.Reports;
using PicoUart.Hid;
using PicoUart.Protocol;
using PicoUart.Tests.Protocol;
using Xunit;

namespace PicoUart.Tests.Hid;

[Trait("Category", "Unit")]
public sealed class ReportsTests
{
    private static ReportDescriptor Descriptor(bool padded, int identitySize = Codec.IdentitySize)
    {
        var bytes = new List<byte>
        {
            0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,
            0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08
        };
        (byte Id, int Size)[] features =
        [
            (1, identitySize), (2, 36), (3, 20), (20, 4),
            .. Enumerable.Range(10, 6).Select(id => ((byte)id, 30)),
            .. Enumerable.Range(30, 6).Select(id => ((byte)id, 24))
        ];
        foreach (var (id, size) in features)
        {
            bytes.AddRange([0x85, id, 0x95, checked((byte)size), 0x09, id, 0xB1, 0x02]);
            if (padded && size < 36)
                bytes.AddRange([0x95, checked((byte)(36 - size)), 0xB1, 0x03]);
        }
        for (byte id = 10; id < 16; id++)
            bytes.AddRange([0x85, id, 0x95, 30, 0x09, id, 0x81, 0x02]);
        bytes.Add(0xC0);
        return new ReportDescriptor(bytes.ToArray());
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void NativeAndWindowsPaddedDescriptorsPreserveProtocolLengths(bool padded)
    {
        var descriptor = Descriptor(padded);
        var features = descriptor.DeviceItems.SelectMany(item => item.FeatureReports).ToArray();
        var inputs = descriptor.DeviceItems.SelectMany(item => item.InputReports).ToArray();
        var identity = Assert.Single(features, report => report.ReportID == Codec.IdentityReportId);

        Assert.Equal(padded ? 37 : 28, identity.Length);
        Assert.Equal(28, Reports.ReportDataLength(identity));
        Reports.ValidateReports(
            features.Select(report => (report.ReportID, Reports.ReportDataLength(report))),
            inputs.Select(report => (report.ReportID, Reports.ReportDataLength(report))));

        var buffer = new byte[37];
        buffer[0] = Codec.IdentityReportId;
        ProtocolFixtures.IdentityPayload().CopyTo(buffer, 1);
        var payload = Reports.FeaturePayload(buffer, Codec.IdentityReportId,
            Reports.ReportDataLength(identity) - 1, prefixSize: 1);
        Assert.Equal(ProtocolFixtures.Identity(), Codec.ParseIdentity(payload));
    }

    [Theory]
    [InlineData(1)]
    [InlineData(2)]
    public void TransportPrefixesAndBufferPaddingAreExcludedFromPayload(int prefixSize)
    {
        var buffer = new byte[36 + prefixSize];
        Array.Fill(buffer, Codec.IdentityReportId, 0, prefixSize);
        ProtocolFixtures.IdentityPayload().CopyTo(buffer, prefixSize);

        var payload = Reports.FeaturePayload(buffer, Codec.IdentityReportId,
            Codec.IdentitySize, prefixSize);

        Assert.Equal(ProtocolFixtures.Identity(), Codec.ParseIdentity(payload));
    }

    [Theory]
    [InlineData(26)]
    [InlineData(28)]
    public void PaddingDoesNotHideIncompatiblePayloadSizes(int identitySize)
    {
        var descriptor = Descriptor(true, identitySize);
        Assert.Throws<InvalidOperationException>(() => Reports.ValidateReports(
            descriptor.DeviceItems.SelectMany(item => item.FeatureReports)
                .Select(report => (report.ReportID, Reports.ReportDataLength(report))),
            descriptor.DeviceItems.SelectMany(item => item.InputReports)
                .Select(report => (report.ReportID, Reports.ReportDataLength(report)))));
    }

    [Fact]
    public void InternalPaddingAndPartialBytesAreRejected()
    {
        var report = new Report();
        report.DataItems.Add(new DataItem { ElementCount = 1, ElementBits = 8, Flags = DataItemFlags.Constant });
        report.DataItems.Add(new DataItem { ElementCount = 1, ElementBits = 8 });
        Assert.Throws<InvalidOperationException>(() => Reports.ReportDataLength(report));

        var partial = new Report();
        partial.DataItems.Add(new DataItem { ElementCount = 1, ElementBits = 7 });
        Assert.Throws<InvalidOperationException>(() => Reports.ReportDataLength(partial));
    }
}
