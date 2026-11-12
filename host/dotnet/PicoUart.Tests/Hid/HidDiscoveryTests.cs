// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using PicoUart.Dashboard;
using PicoUart.Hid;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class HidDiscoveryTests
{
    [Fact]
    public void ExplicitSelectorsFilterDevicesBeforeInspectingDescriptors()
    {
        var devices = new[]
        {
            new Candidate("/healthy", "0123456789ABCDEF", false),
            new Candidate("/unreadable", "8899AABBCCDDEEFF", true)
        };
        bool IsPicoUart(Candidate device)
        {
            if (device.DescriptorFails) throw new IOException("sibling descriptor is unreadable");
            return true;
        }

        var byPath = HidDiscovery.MatchingDevices(devices, null, "/healthy",
            device => device.Path, device => device.Serial, IsPicoUart);
        var bySerial = HidDiscovery.MatchingDevices(devices, "0123456789abcdef", null,
            device => device.Path, device => device.Serial, IsPicoUart);

        Assert.Equal("/healthy", Assert.Single(byPath).Path);
        Assert.Equal("/healthy", Assert.Single(bySerial).Path);
    }

    [Fact]
    public void SerialSelectionIgnoresUnreadableSiblingMetadataWhenAUniqueMatchExists()
    {
        var devices = new[]
        {
            new Candidate("/healthy", "0123456789ABCDEF", false),
            new Candidate("/unreadable", "8899AABBCCDDEEFF", false, true)
        };
        var matches = HidDiscovery.MatchingDevices(devices, "0123456789ABCDEF", null,
            device => device.Path, device => ReadSerial(device), _ => true);

        Assert.Equal("/healthy", Assert.Single(matches).Path);
    }

    [Fact]
    public void SerialSelectionSurfacesMetadataFailureWhenNoReadableMatchExists()
    {
        var device = new Candidate("/unreadable", "0123456789ABCDEF", false, true);

        var error = Assert.Throws<IOException>(() => HidDiscovery.MatchingDevices(
            [device], device.Serial, null, candidate => candidate.Path, ReadSerial, _ => true));

        Assert.Equal("serial metadata is unreadable", error.Message);
    }

    [Fact]
    public async Task DiscoveredBoardPreservesMissingUsbSerialForPathFallback()
    {
        var board = HidDiscovery.CreateBoard("/serial-less", null, ProtocolTests.Identity());
        string? selectedSerial = null;
        string? selectedPath = null;
        using var boards = new DashboardBoardRegistry(null, null, () => [board], (serial, path) =>
        {
            selectedSerial = serial;
            selectedPath = path;
            return new(() => throw new IOException("simulated disconnected board"));
        });

        var catalog = boards.Boards();
        boards.Reader(catalog.DefaultBoardId);

        Assert.Equal("/serial-less", board.DevicePath);
        Assert.Null(board.SerialNumber);
        Assert.Equal("PicoUart", board.ProductName);
        Assert.Null(selectedSerial);
        Assert.Equal("/serial-less", selectedPath);
        await boards.StopAsync(CancellationToken.None);
    }

    private sealed record Candidate(
        string Path, string? Serial, bool DescriptorFails, bool SerialFails = false);

    private static string? ReadSerial(Candidate device) => device.SerialFails
        ? throw new IOException("serial metadata is unreadable")
        : device.Serial;
}
