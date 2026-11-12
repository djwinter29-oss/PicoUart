// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
namespace PicoUart.Hid;

internal sealed class ControlOutcomeUnconfirmedException(string message, Exception? innerException = null)
    : IOException(message, innerException);

internal sealed class ControlRequestRejectedException(string message) : InvalidOperationException(message);

internal static class HidErrors
{
    public static bool IsDeviceError(Exception error) => error is IOException or InvalidOperationException
        or UnauthorizedAccessException or NotSupportedException or TimeoutException;
}