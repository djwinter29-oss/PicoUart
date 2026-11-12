// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Reflection;
using System.Runtime.CompilerServices;
using PicoUart.Hid;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class PicoUartHidActionsTests
{
    [Fact]
    public void ToggleLedPreflightClearsPreviousActionResult()
    {
        var client = (PicoUartHid)RuntimeHelpers.GetUninitializedObject(typeof(PicoUartHid));
        SetPrivateField(client, "gate", new object());
        SetPrivateField(client, "<LastActionResult>k__BackingField", new ActionResult(4, 2, 0));

        Assert.ThrowsAny<Exception>(() => client.ToggleLed());

        Assert.Null(client.LastActionResult);
    }

    [Fact]
    public void WaitForActionReturnsMatchingCompletion()
    {
        var responses = new Queue<ActionResult>([
            new(4, 1, 0),
            new(4, 2, 0)
        ]);
        var result = PicoUartHid.WaitForAction(4, _ => responses.Dequeue());
        Assert.Equal(2, result.Status);
        Assert.Empty(responses);
    }

    [Fact]
    public void WaitForActionRejectsMismatchedOrRejectedResults()
    {
        Assert.Throws<ControlOutcomeUnconfirmedException>(() => PicoUartHid.WaitForAction(4, _ => new(3, 2, 0)));
        Assert.Throws<ControlRequestRejectedException>(() => PicoUartHid.WaitForAction(4, _ => new(4, 5, 6)));
    }

    [Fact]
    public void WaitForActionTimesOutWithoutCompletion()
    {
        var error = Assert.Throws<ControlOutcomeUnconfirmedException>(() =>
            PicoUartHid.WaitForAction(4, _ => new(4, 1, 0), timeoutMs: 1));
        Assert.Contains("outcome is unconfirmed", error.Message);
    }

    private static void SetPrivateField(object target, string name, object? value) =>
        target.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(target, value);
}
