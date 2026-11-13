// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using PicoUart.Hid;
using PicoUart.Protocol;
using Xunit;

namespace PicoUart.Tests.Hid;

[Trait("Category", "Unit")]
public sealed class ActionsTests
{
    [Fact]
    public void RejectionRetainsItsOwnResultAfterAnotherSaveCompletes()
    {
        using var stream = new ScriptedHidStream();
        stream.FeatureWritten = report => stream.Result = report[4] == 2 ? new(4, 5, 6) : new(4, 2, 0);
        using var client = stream.CreateClient();

        var rejected = Assert.Throws<ControlRequestRejectedException>(() => client.SaveConfiguration(2, true));
        var completed = client.SaveConfiguration(0, false);

        Assert.Equal(new ActionResult(4, 5, 6), rejected.Result);
        Assert.Equal(new ConfigurationSaveStatus(2, 0, 0), completed);
        Assert.Equal(2, stream.Writes.Count);
    }

    [Fact]
    public void MismatchedResultQuarantinesConnectionWithoutReplayingTheWrite()
    {
        using var stream = new ScriptedHidStream();
        stream.FeatureWritten = _ => stream.Result = new(3, 2, 0);
        using var client = stream.CreateClient();

        var error = Assert.Throws<ControlOutcomeUnconfirmedException>(() => client.SaveConfiguration(2, true));

        Assert.Null(error.Result);
        Assert.Throws<IOException>(() => client.SaveConfiguration(2, true));
        Assert.Single(stream.Writes);
    }

    [Fact]
    public void RebootPreflightDoesNotReuseAPreviousSaveOutcome()
    {
        using var stream = new ScriptedHidStream();
        using var client = stream.CreateClient();
        client.SaveConfiguration(0, false);

        var error = Assert.Throws<InvalidOperationException>(() => client.SaveConfiguration(2, true));

        Assert.Contains("reboot already scheduled", error.Message);
        Assert.Single(stream.Writes);
    }

    [Fact]
    public async Task ControlCancellationDoesNotWaitForAnOccupiedClientGate()
    {
        using var stream = new ScriptedHidStream();
        using var client = stream.CreateClient();
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        using var cancellation = new CancellationTokenSource();
        stream.InputRead = () =>
        {
            entered.Set();
            if (!release.Wait(TimeSpan.FromSeconds(3)))
                throw new TimeoutException("client gate probe was not released");
            throw new TimeoutException("no simulated Input report");
        };
        var occupyingRead = Task.Run(() => client.ReadStatus());
        try
        {
            Assert.True(entered.Wait(TimeSpan.FromSeconds(3)));
            var save = Task.Run(() => client.SaveConfiguration(0, false, cancellation.Token));
            await Task.Delay(50);
            cancellation.Cancel();

            await Assert.ThrowsAnyAsync<OperationCanceledException>(
                () => save.WaitAsync(TimeSpan.FromSeconds(1)));
            Assert.Empty(stream.Writes);
        }
        finally
        {
            release.Set();
            await occupyingRead;
        }
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task CancellationAfterSetRetainsVerificationAndUncertainty(bool mismatchedResult)
    {
        using var stream = new ScriptedHidStream();
        using var client = stream.CreateClient();
        using var submitted = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        using var cancellation = new CancellationTokenSource();
        stream.FeatureWritten = _ =>
        {
            submitted.Set();
            if (!release.Wait(TimeSpan.FromSeconds(3)))
                throw new TimeoutException("submitted control probe was not released");
            if (mismatchedResult) stream.Result = new(3, 2, 0);
        };
        var save = Task.Run(() => client.SaveConfiguration(0, false, cancellation.Token));
        try
        {
            Assert.True(submitted.Wait(TimeSpan.FromSeconds(3)));
            cancellation.Cancel();
            release.Set();

            if (mismatchedResult)
            {
                await Assert.ThrowsAsync<ControlOutcomeUnconfirmedException>(
                    () => save.WaitAsync(TimeSpan.FromSeconds(2)));
                Assert.Throws<IOException>(() => client.ReadActionResult());
            }
            else
                Assert.Equal((byte)2, (await save.WaitAsync(TimeSpan.FromSeconds(2))).Status);
            Assert.Single(stream.Writes);
        }
        finally
        {
            release.Set();
            if (!save.IsCompleted) await save.WaitAsync(TimeSpan.FromSeconds(3));
        }
    }

    [Fact]
    public void WaitForActionReturnsMatchingCompletion()
    {
        var responses = new Queue<ActionResult>([
            new(4, 1, 0),
            new(4, 2, 0)
        ]);
        var result = Client.WaitForAction(4, _ => responses.Dequeue());
        Assert.Equal(2, result.Status);
        Assert.Empty(responses);
    }

    [Fact]
    public void WaitForActionRejectsMismatchedOrRejectedResults()
    {
        Assert.Throws<ControlOutcomeUnconfirmedException>(() => Client.WaitForAction(4, _ => new(3, 2, 0)));
        Assert.Throws<ControlRequestRejectedException>(() => Client.WaitForAction(4, _ => new(4, 5, 6)));
    }

    [Fact]
    public void WaitForActionTimesOutWithoutCompletion()
    {
        var error = Assert.Throws<ControlOutcomeUnconfirmedException>(() =>
            Client.WaitForAction(4, _ => new(4, 1, 0), timeoutMs: 1));
        Assert.Contains("outcome is unconfirmed", error.Message);
    }

}
