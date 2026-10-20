using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class ConfigurationEndpointTests
{
    [Fact]
    public async Task ConfigurationRouteRequiresSessionCsrfAndStrictJsonAndRejectsUnknownBoards()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        var start = new ProcessStartInfo("dotnet")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false
        };
        start.ArgumentList.Add(typeof(Protocol).Assembly.Location);
        start.ArgumentList.Add("web");
        start.ArgumentList.Add("--port");
        start.ArgumentList.Add(port.ToString(System.Globalization.CultureInfo.InvariantCulture));
        using var process = Process.Start(start)!;
        var output = process.StandardOutput.ReadToEndAsync();
        var errors = process.StandardError.ReadToEndAsync();
        using var handler = new HttpClientHandler { CookieContainer = new CookieContainer() };
        using var http = new HttpClient(handler) { BaseAddress = new($"http://127.0.0.1:{port}"),
            Timeout = TimeSpan.FromSeconds(5) };
        try
        {
            string? page = null;
            for (var attempt = 0; attempt < 200 && !process.HasExited; attempt++)
            {
                try { page = await http.GetStringAsync("/"); break; }
                catch (HttpRequestException) { await Task.Delay(25); }
            }
            Assert.NotNull(page);
            var token = Regex.Match(page, "[A-F0-9]{64}").Value;
            Assert.Equal(64, token.Length);
            const string valid = "{\"profile\":0,\"unsafe_overclock\":false}";
            await AssertResponse(valid, 403);
            http.DefaultRequestHeaders.Add("X-CSRF-Token", "invalid");
            await AssertResponse(valid, 403);
            http.DefaultRequestHeaders.Remove("X-CSRF-Token");
            http.DefaultRequestHeaders.Add("X-CSRF-Token", token);
            await AssertResponse(valid, 415, "text/plain");
            await AssertResponse("{", 400);
            await AssertResponse("{\"profile\":1,\"unsafe_overclock\":false}", 400);
            await AssertResponse("{\"profile\":0,\"unsafe_overclock\":false,\"extra\":0}", 400);
            await AssertResponse(valid, 409);
            using var snapshot = JsonDocument.Parse(await http.GetStringAsync("/api/status"));
            Assert.Equal(JsonValueKind.Null, snapshot.RootElement.GetProperty("configuration_save").ValueKind);
            Assert.Equal(JsonValueKind.Null, snapshot.RootElement.GetProperty("configuration_save_error").ValueKind);
        }
        finally
        {
            if (!process.HasExited) process.Kill(entireProcessTree: true);
            await process.WaitForExitAsync();
            await Task.WhenAll(output, errors);
        }

        async Task AssertResponse(string json, int expectedStatus, string mediaType = "application/json")
        {
            using var content = new StringContent(json, Encoding.UTF8, mediaType);
            using var response = await http.PostAsync("/api/configuration?board=unknown", content);
            Assert.Equal(expectedStatus, (int)response.StatusCode);
            using var body = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            Assert.False(string.IsNullOrEmpty(body.RootElement.GetProperty("error").GetString()));
            Assert.False(body.RootElement.TryGetProperty("ok", out _));
        }
    }
}
