namespace PicoUart.Hid;

internal static class HidErrors
{
    public static bool IsDeviceError(Exception error) => error is IOException or InvalidOperationException
        or UnauthorizedAccessException or NotSupportedException or TimeoutException;
}