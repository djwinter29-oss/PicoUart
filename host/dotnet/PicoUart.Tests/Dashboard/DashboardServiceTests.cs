// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;
using PicoUart.Dashboard;
using PicoUart.Hid;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class DashboardServiceTests
{
    [Fact]
    public void UnconfirmedSaveErrorSurvivesConnectionSnapshotReplacement()
    {
        using var service = new DashboardService(null, null);
        const string warning = "command outcome is unconfirmed; do not retry";

        service.RecordUnconfirmedConfigurationSaveError(warning);
        service.ReplaceSnapshot(new() { Connected = true });

        using var status = JsonDocument.Parse(service.SnapshotJson());
        Assert.True(status.RootElement.GetProperty("connected").GetBoolean());
        Assert.Equal(warning, status.RootElement.GetProperty("configuration_save_error").GetString());
    }

    [Fact]
    public void DiagnosticsFromAReplacedConnectionAreRejected()
    {
        using var service = new DashboardService(null, null);
        var previous = (PicoUartHid)RuntimeHelpers.GetUninitializedObject(typeof(PicoUartHid));
        var current = (PicoUartHid)RuntimeHelpers.GetUninitializedObject(typeof(PicoUartHid));
        typeof(DashboardService).GetField("client", BindingFlags.Instance | BindingFlags.NonPublic)!
            .SetValue(service, current);
        var diagnostics = new ChannelDiagnostics?[6];
        diagnostics[0] = new(0, 1, 0, 0, 7, 0, 0, 0, 0, 0, 0);

        var error = Assert.Throws<IOException>(() => service.PublishDiagnostics(previous, diagnostics, []));

        Assert.Contains("connection changed", error.Message);
        using var status = JsonDocument.Parse(service.SnapshotJson());
        Assert.Equal(JsonValueKind.Null, status.RootElement.GetProperty("channel_diagnostics")[0].ValueKind);
    }
}
