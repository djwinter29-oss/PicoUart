using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.FileProviders;
using PicoUart.Hid;
using PicoUart.Wire;

namespace PicoUart.Dashboard;

internal static class DashboardHost
{
    public static async Task RunAsync(int port, string? serialNumber, string? devicePath)
    {
        var builder = WebApplication.CreateBuilder(Array.Empty<string>());
        builder.Logging.AddFilter("Microsoft",
            builder.Configuration.GetValue("Logging:LogLevel:Microsoft", LogLevel.Warning));
        builder.WebHost.UseUrls($"http://127.0.0.1:{port}");
        builder.Services.AddDistributedMemoryCache();
        builder.Services.AddSession(configuration =>
        {
            configuration.Cookie.Name = "PicoUart.Session";
            configuration.Cookie.HttpOnly = true;
            configuration.Cookie.SameSite = SameSiteMode.Strict;
            configuration.Cookie.IsEssential = true;
        });
        builder.Services.AddSingleton(new DashboardBoards(serialNumber, devicePath));
        builder.Services.AddHostedService(provider => provider.GetRequiredService<DashboardBoards>());
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
        app.MapGet("/api/boards", (DashboardBoards dashboard) =>
        {
            try { return Results.Json(dashboard.Boards(), Protocol.JsonOptions); }
            catch (Exception error) when (HidErrors.IsDeviceError(error))
            {
                return Results.Json(new { error = error.Message }, statusCode: error is IOException ? 503 : 409);
            }
        });
        app.MapGet("/api/status", async (HttpContext context, DashboardBoards dashboard) =>
        {
            await context.Session.LoadAsync();
            try { return Results.Content(dashboard.SnapshotJson(context.Request.Query["board"]), "application/json"); }
            catch (Exception error) when (HidErrors.IsDeviceError(error))
            {
                return Results.Json(new { error = error.Message }, statusCode: error is IOException ? 503 : 409);
            }
        });
        app.MapPost("/api/actions/{action}", (string action, HttpContext context, DashboardBoards dashboard) =>
        {
            var token = context.Session.GetString("csrf_token");
            var supplied = context.Request.Headers["X-CSRF-Token"].ToString();
            if (token is null || !CryptographicOperations.FixedTimeEquals(
                Encoding.UTF8.GetBytes(token), Encoding.UTF8.GetBytes(supplied)))
                return Results.Json(new { error = "request token missing or invalid" }, statusCode: 403);
            if (action is not ("toggle-led" or "reset"))
                return Results.Json(new { error = "unknown action" }, statusCode: 404);
            try { dashboard.Action(context.Request.Query["board"], action); }
            catch (Exception error) when (HidErrors.IsDeviceError(error))
            {
                return Results.Json(new { error = error.Message }, statusCode: error is IOException ? 503 : 409);
            }
            return Results.Json(new { ok = true });
        });
        app.MapPost("/api/configuration", async (HttpContext context, DashboardBoards dashboard) =>
        {
            var token = context.Session.GetString("csrf_token");
            var supplied = context.Request.Headers["X-CSRF-Token"].ToString();
            if (token is null || !CryptographicOperations.FixedTimeEquals(
                Encoding.UTF8.GetBytes(token), Encoding.UTF8.GetBytes(supplied)))
                return Results.Json(new { error = "request token missing or invalid" }, statusCode: 403);
            if (!context.Request.HasJsonContentType())
                return Results.Json(new { error = "configuration request must be JSON" }, statusCode: 415);
            try
            {
                using var body = await JsonDocument.ParseAsync(context.Request.Body,
                    cancellationToken: context.RequestAborted);
                var request = ConfigurationSaveRequest.Parse(body.RootElement);
                dashboard.SaveConfiguration(context.Request.Query["board"], request.Profile, request.UnsafeOverclock);
                return Results.Json(new { ok = true });
            }
            catch (Exception error) when (error is JsonException or ArgumentException
                || HidErrors.IsDeviceError(error))
            {
                var status = error switch
                {
                    JsonException or ArgumentException => 400,
                    TimeoutException => 504,
                    IOException or UnauthorizedAccessException => 503,
                    _ => 409
                };
                return Results.Json(new { error = error.Message }, statusCode: status);
            }
        });
        app.Lifetime.ApplicationStarted.Register(() =>
            Console.WriteLine($"PicoUart dashboard: http://127.0.0.1:{port}/ (Ctrl+C to stop)"));
        await app.RunAsync();
    }
}