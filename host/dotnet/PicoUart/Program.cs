using System.Globalization;
using System.Text.Json;
using PicoUart.Cli;
using PicoUart.Dashboard;
using PicoUart.Hid;
using PicoUart.Wire;

try
{
    var options = Options.Parse(args);
    if (options.Command is "help" or "--help" or "-h")
    {
        Console.WriteLine("PicoUart [--serial SERIAL | --device-path PATH] COMMAND [--port N] [--duration SECONDS] [--json]");
        Console.WriteLine("Commands: status, monitor, overruns, hardware, config, temperature, version, toggle-led, reset, web");
        return 0;
    }
    if (options.Command == "web")
    {
        await DashboardHost.RunAsync(options.Port, options.Serial, options.DevicePath);
        return 0;
    }
    using var client = new PicoUartHid(options.Serial, options.DevicePath);
    switch (options.Command)
    {
        case "status":
            WriteJson(client.ReadStatus(1000)
                ?? throw new InvalidOperationException("no status report arrived before timeout"));
            break;
        case "monitor":
            using (var stop = new CancellationTokenSource())
            {
                Console.CancelKeyPress += (_, eventArgs) => { eventArgs.Cancel = true; stop.Cancel(); };
                if (options.Duration is not null) stop.CancelAfter(TimeSpan.FromSeconds(options.Duration.Value));
                while (!stop.IsCancellationRequested)
                {
                    var status = client.ReadStatus();
                    if (status is not null) WriteJson(status);
                }
            }
            break;
        case "overruns": WriteJson(client.ReadOverflowCounts()); break;
        case "hardware": WriteJson(client.ReadHardwareInfo()); break;
        case "config": WriteJson(client.ReadConfiguration()); break;
        case "temperature":
            Console.WriteLine(client.ReadBoardStatus().TemperatureCelsius.ToString(CultureInfo.InvariantCulture));
            break;
        case "version": Console.WriteLine(client.ReadBoardStatus().FirmwareVersion); break;
        case "toggle-led": client.ToggleLed(); break;
        case "reset": client.ResetBoard(); break;
    }
    return 0;
}
catch (Exception error) when (HidErrors.IsDeviceError(error) || error is ArgumentException)
{
    Console.Error.WriteLine(error.Message);
    return 1;
}

static void WriteJson<TValue>(TValue value) => Console.WriteLine(JsonSerializer.Serialize(value, Protocol.JsonOptions));
