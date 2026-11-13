// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using PicoUart.Protocol;

namespace PicoUart.Hid;

internal sealed class ControlOutcomeUnconfirmedException(
    string message, Exception? innerException = null, ActionResult? result = null)
    : IOException(message, innerException)
{
    public ActionResult? Result { get; } = result;
}

internal sealed class ControlRequestRejectedException(ActionResult result) : InvalidOperationException(
    $"board action rejected: status {result.Status}, error {result.ErrorReason}"
    + (result.Status == 5 ? "; stop UART traffic and close all CDC ports" : ""))
{
    public ActionResult Result { get; } = result;
}

internal static class Errors
{
    public static bool IsDeviceError(Exception error) => error is IOException or InvalidOperationException
        or UnauthorizedAccessException or NotSupportedException or TimeoutException;
}