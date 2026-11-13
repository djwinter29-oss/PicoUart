// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using PicoUart.Cli;
using PicoUart.Hid;
using PicoUart.Protocol;

try
{
    var options = Options.Parse(args);
    if (options.Command is "help" or "--help" or "-h")
    {
        Console.WriteLine("PicoUart [--serial SERIAL | --device-path PATH] COMMAND");
        Console.WriteLine("         [--port N] [--duration SECONDS] [--timeout SECONDS] [--json]");
        Console.WriteLine("Commands: status, monitor, overruns, hardware, config, telemetry, temperature, version, toggle-led, reset, web");
        return 0;
    }
    if (options.Command == "web")
    {
        await PicoUart.Dashboard.Host.RunAsync(options.Port, options.Serial, options.DevicePath);
        return 0;
    }
    using var client = new Client(options.Serial, options.DevicePath);
    switch (options.Command)
    {
        case "status":
            var sample = StatusSnapshot.Poll(
                (readTimeoutMs, operationTimeoutMs) => client.ReadStatus(readTimeoutMs, operationTimeoutMs),
                TimeSpan.FromSeconds(options.StatusTimeout))
                ?? throw new InvalidOperationException("no status report arrived before timeout");
            Console.WriteLine(StatusSnapshot.ToSortedJson(
                StatusSnapshot.FromFirstSample(sample, DateTimeOffset.UtcNow)));
            break;
        case "monitor":
            using (var stop = new CancellationTokenSource())
            {
                Console.CancelKeyPress += (_, eventArgs) => { eventArgs.Cancel = true; stop.Cancel(); };
                var duration = options.Duration is null ? (TimeSpan?)null
                    : TimeSpan.FromSeconds(options.Duration.Value);
                var capture = Stopwatch.StartNew();
                while (!stop.IsCancellationRequested && (duration is null || capture.Elapsed < duration))
                {
                    ChannelStatus? status;
                    if (duration is { } window)
                    {
                        var remaining = window - capture.Elapsed;
                        if (remaining <= TimeSpan.Zero) break;
                        status = StatusSnapshot.ReadWithinDeadline(
                            (readTimeoutMs, operationTimeoutMs) =>
                                client.ReadStatus(readTimeoutMs, operationTimeoutMs),
                            remaining);
                    }
                    else
                        status = client.ReadStatus();
                    if (duration is { } deadline && capture.Elapsed >= deadline) break;
                    if (status is not null) WriteJson(status);
                }
            }
            break;
        case "overruns": WriteJson(client.ReadOverflowCounts()); break;
        case "hardware": WriteJson(client.ReadHardwareInfo()); break;
        case "config": WriteJson(client.ReadConfiguration()); break;
        case "telemetry": WriteJson(client.ReadTelemetry()); break;
        case "temperature":
            Console.WriteLine(client.ReadBoardStatus().TemperatureCelsius.ToString(CultureInfo.InvariantCulture));
            break;
        case "version": Console.WriteLine(client.ReadIdentity().FirmwareVersion); break;
        case "toggle-led":
            client.ToggleLed();
            try { client.ReadTelemetry(); }
            catch (Exception error) when (Errors.IsDeviceError(error))
            {
                Console.Error.WriteLine($"LED action succeeded; telemetry refresh failed: {error.Message}");
            }
            break;
        case "reset": client.ResetBoard(); break;
    }
    return 0;
}
catch (Exception error) when (Errors.IsDeviceError(error) || error is ArgumentException)
{
    Console.Error.WriteLine(error.Message);
    return 1;
}

static void WriteJson<TValue>(TValue value) => Console.WriteLine(JsonSerializer.Serialize(value, Codec.JsonOptions));
