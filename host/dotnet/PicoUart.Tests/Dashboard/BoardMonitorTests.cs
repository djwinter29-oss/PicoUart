// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Text.Json;
using PicoUart.Dashboard;
using PicoUart.Hid;
using PicoUart.Protocol;
using PicoUart.Tests.Hid;
using PicoUart.Tests.Protocol;
using Xunit;

namespace PicoUart.Tests.Dashboard;

[Trait("Category", "Unit")]
public sealed class BoardMonitorTests
{
    [Fact]
    public void UnconfirmedSaveErrorSurvivesConnectionSnapshotReplacement()
    {
        using var service = new BoardMonitor(() => throw new IOException("simulated disconnected board"));
        const string warning = "command outcome is unconfirmed; do not retry";

        service.RecordUnconfirmedConfigurationSaveError(warning);
        service.ReplaceSnapshot(new() { Connected = true });

        using var status = JsonDocument.Parse(service.SnapshotJson());
        Assert.True(status.RootElement.GetProperty("connected").GetBoolean());
        Assert.Equal(warning, status.RootElement.GetProperty("configuration_save_error").GetString());
        Assert.True(status.RootElement.GetProperty("configuration_save_unconfirmed").GetBoolean());
    }

    [Fact]
    public async Task DiagnosticsFromAReplacedConnectionAreRejected()
    {
        using var previousStream = new ScriptedHidStream();
        using var previous = previousStream.CreateClient();
        using var currentStream = new ScriptedHidStream();
        using var current = currentStream.CreateClient();
        using var service = new BoardMonitor(() => current);
        await service.StartAsync(CancellationToken.None);
        try
        {
            using var connected = await WaitForStatus(service,
                status => status.GetProperty("identity").ValueKind != JsonValueKind.Null);
            var diagnostics = Enumerable.Range(0, 6)
                .Select(index => new ChannelDiagnostics(index, 1, 0, 0, 7, 0, 0, 0, 0, 0, 0)).ToArray();

            var error = Assert.Throws<IOException>(() => service.PublishDiagnostics(previous, diagnostics));

            Assert.Contains("connection changed", error.Message);
            using var status = JsonDocument.Parse(service.SnapshotJson());
            Assert.Equal(JsonValueKind.Null, status.RootElement.GetProperty("channel_diagnostics")[0].ValueKind);
        }
        finally { await service.StopAsync(CancellationToken.None); }
    }

    [Fact]
    public async Task RejectedSaveDoesNotReusePreviousActionResultForNewProfile()
    {
        using var stream = new ScriptedHidStream();
        using var connection = stream.CreateClient();
        using var service = new BoardMonitor(() => connection);
        await service.StartAsync(CancellationToken.None);
        try
        {
            using var connected = await WaitForStatus(service,
                status => status.GetProperty("configuration_save").ValueKind != JsonValueKind.Null);
            service.SaveConfiguration(0, false);

            var error = Assert.Throws<InvalidOperationException>(() => service.SaveConfiguration(2, true));

            using var status = JsonDocument.Parse(service.SnapshotJson());
            var save = status.RootElement.GetProperty("configuration_save");
            Assert.Equal((byte)2, save.GetProperty("status").GetByte());
            Assert.Equal((uint)0, save.GetProperty("profile").GetUInt32());
            Assert.False(status.RootElement.GetProperty("configuration_save_unconfirmed").GetBoolean());
            Assert.Contains("reboot already scheduled", error.Message);
            Assert.Single(stream.Writes);
        }
        finally { await service.StopAsync(CancellationToken.None); }
    }

    [Fact]
    public async Task ConcurrentSaveRequestsKeepTheirOwnResultAndPublishInOrder()
    {
        using var stream = new ScriptedHidStream();
        using var connection = stream.CreateClient();
        using var service = new BoardMonitor(() => connection);
        using var submitted = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        stream.FeatureWritten = report =>
        {
            if (report[4] != 2) return;
            submitted.Set();
            if (!release.Wait(TimeSpan.FromSeconds(3))) throw new TimeoutException("save probe was not released");
            stream.Result = new(4, 5, 6);
        };
        await service.StartAsync(CancellationToken.None);
        try
        {
            using var connected = await WaitForStatus(service,
                status => status.GetProperty("configuration_save").ValueKind != JsonValueKind.Null);
            var rejected = Task.Run(() =>
                Assert.Throws<ControlRequestRejectedException>(() => service.SaveConfiguration(2, true)));
            Assert.True(submitted.Wait(TimeSpan.FromSeconds(3)));
            var completed = Task.Run(() => service.SaveConfiguration(0, false));
            release.Set();

            Assert.Equal(new ActionResult(4, 5, 6), (await rejected).Result);
            Assert.Equal((byte)2, (await completed).Status);
            using var status = JsonDocument.Parse(service.SnapshotJson());
            Assert.Equal(0u, status.RootElement.GetProperty("configuration_save").GetProperty("profile").GetUInt32());
            Assert.Equal((byte)2, status.RootElement.GetProperty("configuration_save").GetProperty("status").GetByte());
        }
        finally
        {
            release.Set();
            await service.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task CancelledQueuedSaveDoesNotSubmitAfterEarlierControlCompletes()
    {
        using var stream = new ScriptedHidStream();
        using var connection = stream.CreateClient();
        using var service = new BoardMonitor(() => connection);
        using var submitted = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        var writeCount = 0;
        stream.FeatureWritten = _ =>
        {
            if (Interlocked.Increment(ref writeCount) != 1) return;
            submitted.Set();
            if (!release.Wait(TimeSpan.FromSeconds(3)))
                throw new TimeoutException("blocked save probe was not released");
        };
        await service.StartAsync(CancellationToken.None);
        try
        {
            using var connected = await WaitForStatus(service,
                status => status.GetProperty("configuration_save").ValueKind != JsonValueKind.Null);
            var firstSave = Task.Run(() => service.SaveConfiguration(0, false));
            Assert.True(submitted.Wait(TimeSpan.FromSeconds(3)));
            using var cancellation = new CancellationTokenSource();
            var queuedSave = Task.Run(() => service.SaveConfiguration(1, false, cancellation.Token));
            await Task.Delay(50);
            cancellation.Cancel();

            await Assert.ThrowsAnyAsync<OperationCanceledException>(
                () => queuedSave.WaitAsync(TimeSpan.FromSeconds(1)));
            Assert.Single(stream.Writes);
            release.Set();
            await firstSave;
            Assert.Single(stream.Writes);
        }
        finally
        {
            release.Set();
            await service.StopAsync(CancellationToken.None);
        }
    }

    [Theory]
    [InlineData("toggle-led")]
    [InlineData("reset")]
    [InlineData("save")]
    public async Task CancelledPreflightDoesNotSubmitControlOrRecordUncertainty(string action)
    {
        using var stream = new ScriptedHidStream();
        using var connection = stream.CreateClient();
        using var service = new BoardMonitor(() => connection);
        using var preflight = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        using var cancellation = new CancellationTokenSource();
        var controlRequest = new AsyncLocal<bool>();
        stream.FeatureRead = id =>
        {
            if (id == Codec.ActionReportId && controlRequest.Value)
            {
                preflight.Set();
                if (!release.Wait(TimeSpan.FromSeconds(3)))
                    throw new TimeoutException("control preflight probe was not released");
            }
            return stream.DefaultFeature(id);
        };
        await service.StartAsync(CancellationToken.None);
        try
        {
            using var connected = await WaitForStatus(service,
                status => status.GetProperty("configuration_save").ValueKind != JsonValueKind.Null);
            var control = Task.Run(() =>
            {
                controlRequest.Value = true;
                return action == "save"
                    ? service.SaveConfiguration(0, false, cancellation.Token)
                    : service.Action(action, cancellation.Token);
            });
            Assert.True(preflight.Wait(TimeSpan.FromSeconds(3)));
            Assert.Empty(stream.Writes);
            cancellation.Cancel();
            release.Set();

            await Assert.ThrowsAnyAsync<OperationCanceledException>(
                () => control.WaitAsync(TimeSpan.FromSeconds(2)));
            Assert.Empty(stream.Writes);
            Assert.Equal((byte)0, connection.ReadActionResult().Status);
            using var status = JsonDocument.Parse(service.SnapshotJson());
            Assert.Equal(JsonValueKind.Null, status.RootElement.GetProperty("configuration_save_error").ValueKind);
        }
        finally
        {
            release.Set();
            await service.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task InitialFeatureRetriesPreserveStaleStateUntilAnInputArrives()
    {
        using var stream = new ScriptedHidStream();
        using var connection = stream.CreateClient();
        using var service = new BoardMonitor(() => connection);
        var inputs = new ConcurrentQueue<byte[]>();
        var failedReads = 0;
        stream.FeatureRead = id =>
        {
            if (id == 15)
            {
                Interlocked.Increment(ref failedReads);
                throw new IOException("simulated initial channel Feature failure");
            }
            return id == 10 ? ChannelPayload(2, 200) : stream.DefaultFeature(id);
        };
        stream.InputRead = () =>
        {
            if (inputs.TryDequeue(out var report)) return report;
            Thread.Sleep(stream.ReadTimeout);
            throw new TimeoutException("no simulated Input report");
        };
        await service.StartAsync(CancellationToken.None);
        try
        {
            using var initial = await WaitForStatus(service,
                status => status.GetProperty("configuration_save").ValueKind != JsonValueKind.Null);
            Assert.Equal("ready", initial.RootElement.GetProperty("channels")[0].GetProperty("state").GetString());
            Assert.Equal(JsonValueKind.Null,
                initial.RootElement.GetProperty("channels")[0].GetProperty("sequence").ValueKind);
            var stale = new Snapshot();
            stale.Apply(Codec.ParseChannel(10, ChannelPayload(1, 100), connection.ReadIdentity()));
            stale.Channels[0].LastInputTimestamp -= 4L * System.Diagnostics.Stopwatch.Frequency;
            stale.MarkStale();
            var previousFailures = Volatile.Read(ref failedReads);
            service.ReplaceSnapshot(stale);

            using var retried = await WaitForStatus(service,
                status => status.GetProperty("identity").ValueKind != JsonValueKind.Null
                    && Volatile.Read(ref failedReads) > previousFailures);
            var channel = retried.RootElement.GetProperty("channels")[0];
            Assert.Equal("unknown", channel.GetProperty("state").GetString());
            Assert.Equal(JsonValueKind.Null, channel.GetProperty("cdc_open").ValueKind);
            Assert.Equal(JsonValueKind.Null, channel.GetProperty("uart_measurement_state").ValueKind);
            Assert.Equal(JsonValueKind.Null, channel.GetProperty("baud_rate").ValueKind);
            Assert.Equal(1, channel.GetProperty("traffic_gaps").GetInt32());
            Assert.Equal((ushort)1, channel.GetProperty("sequence").GetUInt16());
            Assert.Equal(100, channel.GetProperty("totals").GetProperty("uart_tx").GetDouble());

            inputs.Enqueue([10, .. ChannelPayload(0, 300)]);
            using var recovered = await WaitForStatus(service,
                status => status.GetProperty("channels")[0].GetProperty("sequence").GetUInt16() == 0);
            channel = recovered.RootElement.GetProperty("channels")[0];
            Assert.Equal("ready", channel.GetProperty("state").GetString());
            Assert.True(channel.GetProperty("cdc_open").GetBoolean());
            Assert.Equal(300, channel.GetProperty("totals").GetProperty("uart_tx").GetDouble());
            Assert.Equal(JsonValueKind.Null, channel.GetProperty("traffic").GetProperty("uart_tx").ValueKind);
        }
        finally { await service.StopAsync(CancellationToken.None); }

        byte[] ChannelPayload(ushort sequence, uint total)
        {
            var payload = stream.DefaultFeature(10);
            payload[1] = 2;
            BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(3), 115200);
            payload[7] = 8;
            payload[8] = 1;
            BinaryPrimitives.WriteUInt16LittleEndian(payload.AsSpan(10), sequence);
            BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(14), total);
            return payload;
        }
    }

    [Theory]
    [InlineData(30)]
    [InlineData(31)]
    public async Task FailedDiagnosticsRefreshIsExplicitAndRetainsLastSuccessfulMeasurements(byte failingReport)
    {
        using var stream = new ScriptedHidStream();
        using var connection = stream.CreateClient();
        using var service = new BoardMonitor(() => connection);
        var fail = false;
        stream.FeatureRead = id =>
        {
            if (fail && id == failingReport) throw new IOException("simulated diagnostics read failure");
            var payload = stream.DefaultFeature(id);
            if (id == 30) BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4), fail ? 9u : 7u);
            return payload;
        };
        await service.StartAsync(CancellationToken.None);
        try
        {
            using var connected = await WaitForStatus(service,
                status => status.GetProperty("configuration_save").ValueKind != JsonValueKind.Null);
            service.DiagnosticsJson();
            fail = true;

            Assert.Throws<IOException>(() => service.DiagnosticsJson());

            using var status = JsonDocument.Parse(service.SnapshotJson());
            Assert.Equal(7u, status.RootElement.GetProperty("overflow_counts")[0].GetUInt32());
            Assert.Equal("simulated diagnostics read failure",
                status.RootElement.GetProperty("diagnostics_error").GetString());
            fail = false;
            service.DiagnosticsJson();
            using var recovered = JsonDocument.Parse(service.SnapshotJson());
            Assert.Equal(JsonValueKind.Null, recovered.RootElement.GetProperty("diagnostics_error").ValueKind);
        }
        finally { await service.StopAsync(CancellationToken.None); }
    }

    [Theory]
    [InlineData(0, 0, 0, 0)]
    [InlineData(2, 2, 0, 0)]
    [InlineData(4, 1, 0, 1)]
    [InlineData(4, 2, 0, 2)]
    [InlineData(4, 5, 6, 5)]
    public async Task MetadataProjectsReport20IntoSaveStatus(
        byte action, byte resultStatus, byte errorReason, byte expected)
    {
        using var stream = new ScriptedHidStream { Result = new(action, resultStatus, errorReason) };
        using var connection = stream.CreateClient();
        using var service = new BoardMonitor(() => connection);
        await service.StartAsync(CancellationToken.None);
        try
        {
            using var status = await WaitForStatus(service,
                value => value.GetProperty("configuration_save").ValueKind != JsonValueKind.Null);
            var save = status.RootElement.GetProperty("configuration_save");
            Assert.Equal(expected, save.GetProperty("status").GetByte());
            Assert.Equal(JsonValueKind.Null, save.GetProperty("profile").ValueKind);
            Assert.Empty(stream.Writes);
        }
        finally { await service.StopAsync(CancellationToken.None); }
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task UnavailableCapabilityAndFailedControlReadAreExplicit(bool readFails)
    {
        using var stream = new ScriptedHidStream();
        stream.FeatureRead = id => id == 20
            ? throw new IOException("simulated control read failure") : stream.DefaultFeature(id);
        using var connection = stream.CreateClient(
            ProtocolFixtures.Identity() with { Capabilities = readFails ? (byte)7 : (byte)3 });
        using var service = new BoardMonitor(() => connection);
        await service.StartAsync(CancellationToken.None);
        try
        {
            using var status = await WaitForStatus(service,
                value => value.GetProperty("configuration_save_error").ValueKind != JsonValueKind.Null);
            Assert.Equal(JsonValueKind.Null, status.RootElement.GetProperty("configuration_save").ValueKind);
            Assert.Equal(readFails ? "simulated control read failure" : "configuration save capability is unavailable",
                status.RootElement.GetProperty("configuration_save_error").GetString());
            Assert.Empty(stream.Writes);
        }
        finally { await service.StopAsync(CancellationToken.None); }
    }

    [Fact]
    public async Task UnconfirmedWarningSurvivesRetainedControlResultUntilNewSaveIsConfirmed()
    {
        using var stream = new ScriptedHidStream();
        stream.Result = new(4, 5, 6);
        var controlReads = 0;
        stream.FeatureRead = id =>
        {
            if (id == Codec.ActionReportId) Interlocked.Increment(ref controlReads);
            return stream.DefaultFeature(id);
        };
        using var connection = stream.CreateClient();
        using var service = new BoardMonitor(() => connection);
        service.RecordUnconfirmedConfigurationSaveError("save outcome is unconfirmed");
        await service.StartAsync(CancellationToken.None);
        try
        {
            using var initial = await WaitForStatus(service,
                value => value.GetProperty("runtime").ValueKind != JsonValueKind.Null);
            Assert.Equal("save outcome is unconfirmed",
                initial.RootElement.GetProperty("configuration_save_error").GetString());
            Assert.Equal(JsonValueKind.Null, initial.RootElement.GetProperty("configuration_save").ValueKind);
            Assert.True(SpinWait.SpinUntil(
                () => Volatile.Read(ref controlReads) >= 1, TimeSpan.FromSeconds(3)));
            await Task.Delay(50);
            using var retained = JsonDocument.Parse(service.SnapshotJson());
            Assert.Equal("save outcome is unconfirmed",
                retained.RootElement.GetProperty("configuration_save_error").GetString());
            Assert.Equal(JsonValueKind.Null, retained.RootElement.GetProperty("configuration_save").ValueKind);
            Assert.True(retained.RootElement.GetProperty("configuration_save_unconfirmed").GetBoolean());

            stream.Result = new(0, 0, 0);
            service.SaveConfiguration(0, false);
            using var recovered = JsonDocument.Parse(service.SnapshotJson());
            Assert.Equal(JsonValueKind.Null, recovered.RootElement.GetProperty("configuration_save_error").ValueKind);
            Assert.False(recovered.RootElement.GetProperty("configuration_save_unconfirmed").GetBoolean());
            Assert.Equal((byte)2,
                recovered.RootElement.GetProperty("configuration_save").GetProperty("status").GetByte());
        }
        finally { await service.StopAsync(CancellationToken.None); }
    }

    private static async Task<JsonDocument> WaitForStatus(BoardMonitor service, Func<JsonElement, bool> predicate)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        while (true)
        {
            var status = JsonDocument.Parse(service.SnapshotJson());
            if (predicate(status.RootElement)) return status;
            status.Dispose();
            await Task.Delay(10, timeout.Token);
        }
    }
}
