// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Diagnostics;
using PicoUart.Wire;

namespace PicoUart.Hid;

// Actions share the connection gate and timeout quarantine; they are not a separate client.
public sealed partial class PicoUartHid
{
    private bool rebootScheduled;
    internal ActionResult? LastActionResult { get; private set; }

    public ActionResult ToggleLed()
    {
        lock (gate)
        {
            var output = ReadLed().OutputOn
                ?? throw new InvalidOperationException("board identification LED unavailable");
            return SendAction(output ? (byte)1 : (byte)2);
        }
    }

    public ActionResult ResetBoard()
    {
        lock (gate) return SendAction(3);
    }

    /// <summary>Return only after report 20 confirms the save and automatic reboot schedule.</summary>
    public ConfigurationSaveStatus SaveConfiguration(uint profile, bool unsafeOverclock)
    {
        lock (gate)
        {
            var result = SendAction(4, profile, unsafeOverclock);
            return new(result.Status, result.ErrorReason, profile);
        }
    }

    private ActionResult SendAction(byte action, uint profile = 0, bool unsafeOverclock = false)
    {
        CheckConnection();
        if (rebootScheduled) throw new InvalidOperationException("board reboot already scheduled; reconnect");
        var info = ReadIdentity();
        LastActionResult = null;
        var prior = ReadActionResultCore(3000);
        if (prior.Status == 1 || (prior.Status == 2 && prior.Action is 3 or 4))
            throw new InvalidOperationException("a board action is pending or reboot is already scheduled; reconnect");
        var payload = Protocol.BuildAction(action, info, profile, unsafeOverclock);
        byte[] report = [Protocol.ActionReportId, .. payload];
        var submissionAttempted = false;
        try
        {
            submissionAttempted = true;
            Transfer(() => { stream.SetFeature(report); return true; }, 3000);
            var result = WaitForAction(action, timeout =>
            {
                var observed = ReadActionResultCore(timeout);
                LastActionResult = observed;
                return observed;
            });
            rebootScheduled = action is 3 or 4;
            return result;
        }
        catch (ControlRequestRejectedException)
        {
            throw;
        }
        catch (Exception error) when (submissionAttempted && HidErrors.IsDeviceError(error))
        {
            transferUnconfirmed = true;
            throw Unconfirmed("command outcome is unconfirmed; do not retry; reconnect and verify board state", error);
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
                throw new ControlRequestRejectedException(
                    $"board action rejected: status {result.Status}, error {result.ErrorReason}"
                    + (result.Status == 5 ? "; stop UART traffic and close all CDC ports" : ""));
            if (result.Status != 1)
                throw Unconfirmed("board action outcome is unconfirmed: no matching result");
            remaining = timeoutMs - (int)timer.ElapsedMilliseconds;
            if (remaining > 0) Thread.Sleep(Math.Min(50, remaining));
        }
    }

    private static ControlOutcomeUnconfirmedException Unconfirmed(string message, Exception? innerException = null) =>
        new(message, innerException);
}
