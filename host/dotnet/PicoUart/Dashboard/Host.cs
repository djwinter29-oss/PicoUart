// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.FileProviders;
using PicoUart.Hid;

namespace PicoUart.Dashboard;

internal static class Host
{
    private const int MaxConfigurationRequestBytes = 1024;

    public static async Task RunAsync(int port, string? serialNumber, string? devicePath)
    {
        await using var app = await CreateApplicationAsync(port, serialNumber: serialNumber, devicePath: devicePath);
        await app.RunAsync();
    }

    internal static async Task<WebApplication> CreateApplicationAsync(
        int port, IBoardRegistry? dashboard = null, string? serialNumber = null, string? devicePath = null)
    {
        var builder = WebApplication.CreateBuilder(Array.Empty<string>());
        builder.Logging.AddFilter("Microsoft",
            builder.Configuration.GetValue("Logging:LogLevel:Microsoft", LogLevel.Warning));
        builder.WebHost.UseUrls($"http://127.0.0.1:{port}");
        builder.Services.AddDistributedMemoryCache();
        builder.Services.AddSession(configuration =>
        {
            configuration.Cookie.Name = $"PicoUart.DotNet.Session.{port}";
            configuration.Cookie.HttpOnly = true;
            configuration.Cookie.SameSite = SameSiteMode.Strict;
            configuration.Cookie.IsEssential = true;
        });
        if (dashboard is null)
        {
            builder.Services.AddSingleton<IBoardRegistry>(services => new BoardRegistry(
                serialNumber, devicePath, services.GetRequiredService<ILoggerFactory>()));
            builder.Services.AddSingleton<IHostedService>(services =>
                (BoardRegistry)services.GetRequiredService<IBoardRegistry>());
        }
        else
        {
            builder.Services.AddSingleton(dashboard);
            if (dashboard is IHostedService hostedService)
                builder.Services.AddSingleton<IHostedService>(hostedService);
        }
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
                FileProvider = new PhysicalFileProvider(Path.Combine(webRoot, directory)),
                RequestPath = $"/{directory}"
            });
        }
        var template = await File.ReadAllTextAsync(Path.Combine(webRoot, "index.html"));
        var packagedVersion = Assembly.GetExecutingAssembly()
            .GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "0.0.0-debug";
        app.MapGet("/favicon.svg", () => Results.File(Path.Combine(webRoot, "favicon.svg"), "image/svg+xml"));
        app.MapGet("/", (HttpContext context) =>
        {
            var token = context.Session.GetString("csrf_token");
            if (token is null)
            {
                token = Convert.ToHexString(RandomNumberGenerator.GetBytes(32));
                context.Session.SetString("csrf_token", token);
            }
            var hostVersion = Environment.GetEnvironmentVariable("PICO_UART_HOST_VERSION");
            if (string.IsNullOrEmpty(hostVersion)) hostVersion = packagedVersion;
            return Results.Content(template.Replace("{{ csrf_token }}", token).Replace("{{ dashboard_type }}", ".NET")
                .Replace("{{ host_version }}", System.Net.WebUtility.HtmlEncode(hostVersion)),
                "text/html");
        });
        app.MapGet("/api/boards", (IBoardRegistry dashboard) =>
        {
            try { return Results.Json(dashboard.Boards(), Codec.JsonOptions); }
            catch (Exception error) when (Errors.IsDeviceError(error))
            {
                return DeviceErrorResponse(error);
            }
        });
        app.MapGet("/api/status", async (HttpContext context, IBoardRegistry dashboard) =>
        {
            await context.Session.LoadAsync();
            try { return Results.Content(dashboard.SnapshotJson(context.Request.Query["board"]), "application/json"); }
            catch (Exception error) when (Errors.IsDeviceError(error))
            {
                return DeviceErrorResponse(error);
            }
        });
        app.MapGet("/api/diagnostics", async (HttpContext context, IBoardRegistry dashboard) =>
        {
            await context.Session.LoadAsync();
            try
            {
                return Results.Content(
                    dashboard.DiagnosticsJson(context.Request.Query["board"]), "application/json");
            }
            catch (Exception error) when (Errors.IsDeviceError(error))
            {
                return DeviceErrorResponse(error);
            }
        });
        app.MapPost("/api/actions/{action}", (
            string action, HttpContext context, IBoardRegistry dashboard, ILoggerFactory loggerFactory) =>
        {
            var logger = loggerFactory.CreateLogger("PicoUart.Dashboard.Host");
            var token = context.Session.GetString("csrf_token");
            var supplied = context.Request.Headers["X-CSRF-Token"].ToString();
            if (token is null || !CryptographicOperations.FixedTimeEquals(
                Encoding.UTF8.GetBytes(token), Encoding.UTF8.GetBytes(supplied)))
                return Results.Json(new { error = "request token missing or invalid" }, statusCode: 403);
            if (action is not ("toggle-led" or "reset"))
                return Results.Json(new { error = "unknown action" }, statusCode: 404);
            try
            {
                return Results.Json(dashboard.Action(
                    context.Request.Query["board"], action, context.RequestAborted), Codec.JsonOptions);
            }
            catch (Exception error) when (Errors.IsDeviceError(error))
            {
                logger.LogWarning(error, "HID control action {Action} failed", action);
                return DeviceErrorResponse(error);
            }
        });
        app.MapPost("/api/configuration", async (
            HttpContext context, IBoardRegistry dashboard, ILoggerFactory loggerFactory) =>
        {
            var logger = loggerFactory.CreateLogger("PicoUart.Dashboard.Host");
            if (context.Request.ContentLength > MaxConfigurationRequestBytes)
                return ConfigurationRequestTooLarge();
            var requestBody = await ReadConfigurationBodyAsync(context.Request.Body, context.RequestAborted);
            if (requestBody is null) return ConfigurationRequestTooLarge();
            var token = context.Session.GetString("csrf_token");
            var supplied = context.Request.Headers["X-CSRF-Token"].ToString();
            if (token is null || !CryptographicOperations.FixedTimeEquals(
                Encoding.UTF8.GetBytes(token), Encoding.UTF8.GetBytes(supplied)))
                return Results.Json(new { error = "request token missing or invalid" }, statusCode: 403);
            if (!context.Request.HasJsonContentType())
                return Results.Json(new { error = "configuration request must be JSON" }, statusCode: 415);
            try
            {
                using var body = JsonDocument.Parse(requestBody);
                var request = ConfigurationSaveRequest.Parse(body.RootElement);
                var result = dashboard.SaveConfiguration(
                    context.Request.Query["board"], request.Profile, request.UnsafeOverclock, context.RequestAborted);
                return Results.Json(result, Codec.JsonOptions);
            }
            catch (Exception error) when (error is JsonException or ArgumentException
                || Errors.IsDeviceError(error))
            {
                logger.LogWarning(error, "HID configuration save failed");
                return error is JsonException or ArgumentException
                    ? ErrorResponse(error, 400) : DeviceErrorResponse(error);
            }
        });
        app.Lifetime.ApplicationStarted.Register(() =>
            Console.WriteLine($"PicoUart dashboard: http://127.0.0.1:{port}/ (Ctrl+C to stop)"));
        return app;
    }

    private static IResult DeviceErrorResponse(Exception error) =>
        Results.Json(new
        {
            error = error.Message,
            outcome_unconfirmed = error is ControlOutcomeUnconfirmedException
        }, Codec.JsonOptions, statusCode: error switch
        {
            TimeoutException => 504,
            IOException or UnauthorizedAccessException => 503,
            _ => 409
        });

    private static IResult ErrorResponse(Exception error, int statusCode) =>
        Results.Json(new { error = error.Message }, statusCode: statusCode);

    private static IResult ConfigurationRequestTooLarge() =>
        Results.Json(new
        {
            error = $"configuration request exceeds {MaxConfigurationRequestBytes} bytes"
        }, statusCode: 413);

    private static async Task<byte[]?> ReadConfigurationBodyAsync(
        Stream requestBody, CancellationToken cancellationToken)
    {
        var buffer = new byte[MaxConfigurationRequestBytes + 1];
        var length = 0;
        while (length < buffer.Length)
        {
            var read = await requestBody.ReadAsync(buffer.AsMemory(length), cancellationToken);
            if (read == 0) return buffer.AsSpan(0, length).ToArray();
            length += read;
        }
        return null;
    }
}

internal sealed record ConfigurationSaveRequest(uint Profile, bool UnsafeOverclock)
{
    public static ConfigurationSaveRequest Parse(JsonElement body)
    {
        if (body.ValueKind != JsonValueKind.Object || body.EnumerateObject().Count() != 2
            || !body.TryGetProperty("profile", out var profile) || profile.ValueKind != JsonValueKind.Number
            || !profile.TryGetUInt32(out var value) || value > 4
            || !body.TryGetProperty("unsafe_overclock", out var acknowledgement)
            || acknowledgement.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
            throw new ArgumentException("expected profile (integer 0..4) and unsafe_overclock (boolean)");
        var unsafeOverclock = acknowledgement.GetBoolean();
        if (value != 0 && !unsafeOverclock)
            throw new ArgumentException("explicit unsafe overclock acknowledgement is required");
        return new(value, unsafeOverclock);
    }
}
