using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.FileProviders;
using PicoUart;

try
{
    var options = Options.Parse(args);
    if (options.Command is "help" or "--help" or "-h")
    {
        Console.WriteLine("PicoUart [--serial SERIAL | --device-path PATH] COMMAND [--port N] [--duration SECONDS] [--json]");
        Console.WriteLine("Commands: status, monitor, overruns, hardware, temperature, version, toggle-led, reset, web");
        return 0;
    }
    if (options.Command == "web")
    {
        await RunWeb(options);
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
        case "temperature":
            Console.WriteLine(client.ReadBoardStatus().TemperatureCelsius.ToString(CultureInfo.InvariantCulture));
            break;
        case "version": Console.WriteLine(client.ReadBoardStatus().FirmwareVersion); break;
        case "toggle-led": client.ToggleLed(); break;
        case "reset": client.ResetBoard(); break;
    }
    return 0;
}
catch (Exception error) when (DashboardService.IsDeviceError(error) || error is ArgumentException)
{
    Console.Error.WriteLine(error.Message);
    return 1;
}

static void WriteJson<TValue>(TValue value) => Console.WriteLine(JsonSerializer.Serialize(value, Protocol.JsonOptions));

static async Task RunWeb(Options options)
{
    var builder = WebApplication.CreateBuilder(Array.Empty<string>());
    builder.WebHost.UseUrls($"http://127.0.0.1:{options.Port}");
    builder.Services.AddDistributedMemoryCache();
    builder.Services.AddSession(configuration =>
    {
        configuration.Cookie.Name = "PicoUart.Session";
        configuration.Cookie.HttpOnly = true;
        configuration.Cookie.SameSite = SameSiteMode.Strict;
        configuration.Cookie.IsEssential = true;
    });
    builder.Services.AddSingleton(new DashboardService(options.Serial, options.DevicePath));
    builder.Services.AddHostedService(provider => provider.GetRequiredService<DashboardService>());
    var app = builder.Build();
    app.Use(async (context, next) =>
    {
        if (context.Request.Host.Host is not ("localhost" or "127.0.0.1"))
        {
            context.Response.StatusCode = 400;
            return;
        }
        context.Response.Headers["X-Content-Type-Options"] = "nosniff";
        context.Response.Headers["X-Frame-Options"] = "DENY";
        context.Response.Headers["Referrer-Policy"] = "no-referrer";
        context.Response.Headers["Content-Security-Policy"] = "default-src 'self'; connect-src 'self'; img-src 'self'; "
            + "style-src 'self'; script-src 'self'; frame-ancestors 'none'";
        context.Response.Headers.CacheControl = "no-store";
        await next(context);
    });
    app.UseSession();
    var webRoot = Path.Combine(AppContext.BaseDirectory, "web");
    foreach (var directory in new[] { "css", "js" })
    {
        app.UseStaticFiles(new StaticFileOptions
        {
            FileProvider = new PhysicalFileProvider(Path.Combine(webRoot, directory)), RequestPath = $"/{directory}"
        });
    }
    var template = await File.ReadAllTextAsync(Path.Combine(webRoot, "index.html"));
    app.MapGet("/favicon.svg", () => Results.File(Path.Combine(webRoot, "favicon.svg"), "image/svg+xml"));
    app.MapGet("/", (HttpContext context) =>
    {
        var token = context.Session.GetString("csrf_token");
        if (token is null)
        {
            token = Convert.ToHexString(RandomNumberGenerator.GetBytes(32));
            context.Session.SetString("csrf_token", token);
        }
        return Results.Content(template.Replace("{{ csrf_token }}", token), "text/html");
    });
    app.MapGet("/api/status", async (HttpContext context, DashboardService dashboard) =>
    {
        await context.Session.LoadAsync();
        return Results.Content(dashboard.SnapshotJson(), "application/json");
    });
    app.MapPost("/api/actions/{action}", (string action, HttpContext context, DashboardService dashboard) =>
    {
        var token = context.Session.GetString("csrf_token");
        var supplied = context.Request.Headers["X-CSRF-Token"].ToString();
        if (token is null || !CryptographicOperations.FixedTimeEquals(
            Encoding.UTF8.GetBytes(token), Encoding.UTF8.GetBytes(supplied)))
            return Results.Json(new { error = "request token missing or invalid" }, statusCode: 403);
        if (action is not ("toggle-led" or "reset"))
            return Results.Json(new { error = "unknown action" }, statusCode: 404);
        try { dashboard.Action(action); }
        catch (Exception error) when (DashboardService.IsDeviceError(error))
        {
            return Results.Json(new { error = error.Message }, statusCode: error is IOException ? 503 : 409);
        }
        return Results.Json(new { ok = true });
    });
    await app.RunAsync();
}

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
        if (command is not ("help" or "--help" or "-h" or "status" or "monitor" or "overruns" or "hardware"
            or "temperature" or "version" or "toggle-led" or "reset" or "web"))
            throw new ArgumentException($"unknown command: {command}");
        return new(command, serial, devicePath, port, duration);
    }
}