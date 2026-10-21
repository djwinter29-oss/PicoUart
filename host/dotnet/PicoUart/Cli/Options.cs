using System.Globalization;

namespace PicoUart.Cli;

internal sealed record Options(string Command, string? Serial, string? DevicePath, int Port, double? Duration)
{
    public static Options Parse(string[] arguments)
    {
        string? command = null, serial = null, devicePath = null;
        var port = 5000;
        double? duration = null;
        for (var index = 0; index < arguments.Length; index++)
        {
            var argument = arguments[index];
            if (argument == "--json") continue;
            if (argument is "--serial" or "--device-path" or "--port" or "--duration")
            {
                if (++index == arguments.Length) throw new ArgumentException($"missing value for {argument}");
                var value = arguments[index];
                switch (argument)
                {
                    case "--serial": serial = value; break;
                    case "--device-path": devicePath = value; break;
                    case "--port":
                        if (!int.TryParse(value, out port) || port is < 1 or > 65535)
                            throw new ArgumentException("port must be between 1 and 65535");
                        break;
                    case "--duration":
                        if (!double.TryParse(value, CultureInfo.InvariantCulture, out var seconds)
                            || !double.IsFinite(seconds) || seconds <= 0 || seconds > uint.MaxValue / 1000.0)
                            throw new ArgumentException("duration must be a finite positive number within timer range");
                        duration = seconds;
                        break;
                }
            }
            else if (command is null) command = argument;
            else throw new ArgumentException($"unexpected argument: {argument}");
        }
        if (serial is not null && devicePath is not null)
            throw new ArgumentException("--serial and --device-path cannot be used together");
        command ??= "help";
        if (command is not ("help" or "--help" or "-h" or "status" or "monitor" or "overruns" or "hardware" or "config"
            or "temperature" or "version" or "toggle-led" or "reset" or "web"))
            throw new ArgumentException($"unknown command: {command}");
        return new(command, serial, devicePath, port, duration);
    }
}