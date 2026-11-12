// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Diagnostics;
using PicoUart.Wire;

namespace PicoUart.Hid;

// Actions share the connection gate and timeout quarantine; they are not a separate client.
public sealed partial class PicoUartHid
{
    private bool rebootScheduled;

    public ActionResult ToggleLed(CancellationToken cancellationToken = default)
    {
        EnterControlGate(cancellationToken);
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            var output = ReadLed().OutputOn
                ?? throw new InvalidOperationException("board identification LED unavailable");
            return SendAction(output ? (byte)1 : (byte)2, cancellationToken: cancellationToken);
        }
        finally { Monitor.Exit(gate); }
    }

    public ActionResult ResetBoard(CancellationToken cancellationToken = default)
    {
        EnterControlGate(cancellationToken);
        try { return SendAction(3, cancellationToken: cancellationToken); }
        finally { Monitor.Exit(gate); }
    }

    /// <summary>Return only after report 20 confirms the save and automatic reboot schedule.</summary>
    public ConfigurationSaveStatus SaveConfiguration(
        uint profile, bool unsafeOverclock, CancellationToken cancellationToken = default)
    {
        EnterControlGate(cancellationToken);
        try
        {
            var result = SendAction(4, profile, unsafeOverclock, cancellationToken);
            return new(result.Status, result.ErrorReason, profile);
        }
        finally { Monitor.Exit(gate); }
    }

    /// <summary>Retain reentrant typed-read locking while making control acquisition cancellable.</summary>
    private void EnterControlGate(CancellationToken cancellationToken)
    {
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (Monitor.TryEnter(gate, 50)) return;
        }
    }

    private ActionResult SendAction(byte action, uint profile = 0, bool unsafeOverclock = false,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        CheckConnection();
        if (rebootScheduled) throw new InvalidOperationException("board reboot already scheduled; reconnect");
        var info = ReadIdentity();
        cancellationToken.ThrowIfCancellationRequested();
        var prior = ReadActionResultCore(3000);
        cancellationToken.ThrowIfCancellationRequested();
        if (prior.Status == 1 || (prior.Status == 2 && prior.Action is 3 or 4))
            throw new InvalidOperationException("a board action is pending or reboot is already scheduled; reconnect");
        var payload = Protocol.BuildAction(action, info, profile, unsafeOverclock);
        byte[] report = [Protocol.ActionReportId, .. payload];
        ActionResult? lastResult = null;
        try
        {
            Transfer(() =>
            {
                cancellationToken.ThrowIfCancellationRequested();
                stream.SetFeature(report);
                return true;
            }, 3000);
            var result = WaitForAction(action, timeout =>
            {
                var observed = ReadActionResultCore(timeout);
                if (observed.Action == action) lastResult = observed;
                return observed;
            });
            rebootScheduled = action is 3 or 4;
            return result;
        }
        catch (ControlRequestRejectedException)
        {
            throw;
        }
        catch (Exception error) when (HidErrors.IsDeviceError(error))
        {
            transferUnconfirmed = true;
            throw Unconfirmed("command outcome is unconfirmed; do not retry; reconnect and verify board state",
                error, lastResult);
        }
    }

    internal static ActionResult WaitForAction(byte action, Func<int, ActionResult> read, int timeoutMs = 3000)
    {
        var timer = Stopwatch.StartNew();
        while (true)
        {
            var remaining = timeoutMs - (int)timer.ElapsedMilliseconds;
            if (remaining <= 0)
                throw Unconfirmed("board action outcome is unconfirmed; reconnect before further I/O");
            var result = read(remaining);
            if (timer.ElapsedMilliseconds >= timeoutMs)
                throw Unconfirmed("board action outcome is unconfirmed; reconnect before further I/O");
            if (result.Action != action)
                throw Unconfirmed("board action outcome is unconfirmed: result belongs to another action");
            if (result.Status == 2) return result;
            if (result.Status is >= 3 and <= 6)
                throw new ControlRequestRejectedException(result);
            if (result.Status != 1)
                throw Unconfirmed("board action outcome is unconfirmed: no matching result");
            remaining = timeoutMs - (int)timer.ElapsedMilliseconds;
            if (remaining > 0) Thread.Sleep(Math.Min(50, remaining));
        }
    }

    private static ControlOutcomeUnconfirmedException Unconfirmed(
        string message, Exception? innerException = null, ActionResult? result = null) =>
        new(message, innerException, result);
}
