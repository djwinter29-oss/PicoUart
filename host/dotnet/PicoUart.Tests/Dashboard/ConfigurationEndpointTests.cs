using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using PicoUart.Dashboard;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class ConfigurationEndpointTests
{
    [Theory]
    [InlineData("{\"profile\":0,\"unsafe_overclock\":false}", 0u, false)]
    [InlineData("{\"profile\":1,\"unsafe_overclock\":true}", 1u, true)]
    [InlineData("{\"unsafe_overclock\":true,\"profile\":2}", 2u, true)]
    [InlineData("{\"profile\":3,\"unsafe_overclock\":true}", 3u, true)]
    [InlineData("{\"profile\":4,\"unsafe_overclock\":true}", 4u, true)]
    public void AcceptsStrictDashboardRequest(string json, uint profile, bool acknowledgement)
    {
        using var body = JsonDocument.Parse(json);
        Assert.Equal(new ConfigurationSaveRequest(profile, acknowledgement), ConfigurationSaveRequest.Parse(body.RootElement));
    }

    [Theory]
    [InlineData("null")]
    [InlineData("[]")]
    [InlineData("{}")]
    [InlineData("{\"profile\":0}")]
    [InlineData("{\"profile\":0,\"unsafe_overclock\":false,\"extra\":true}")]
    [InlineData("{\"profile\":0,\"profile\":1,\"unsafe_overclock\":true}")]
    [InlineData("{\"profile\":true,\"unsafe_overclock\":true}")]
    [InlineData("{\"profile\":\"0\",\"unsafe_overclock\":false}")]
    [InlineData("{\"profile\":0.5,\"unsafe_overclock\":false}")]
    [InlineData("{\"profile\":-1,\"unsafe_overclock\":false}")]
    [InlineData("{\"profile\":5,\"unsafe_overclock\":true}")]
    [InlineData("{\"profile\":4294967296,\"unsafe_overclock\":true}")]
    [InlineData("{\"profile\":0,\"unsafe_overclock\":1}")]
    [InlineData("{\"profile\":0,\"unsafe_overclock\":null}")]
    [InlineData("{\"profile\":1,\"unsafe_overclock\":false}")]
    [InlineData("{\"profile\":2,\"unsafe_overclock\":false}")]
    [InlineData("{\"profile\":3,\"unsafe_overclock\":false}")]
    [InlineData("{\"profile\":4,\"unsafe_overclock\":false}")]
    public void RejectsInvalidDashboardRequests(string json)
    {
        using var body = JsonDocument.Parse(json);
        Assert.Throws<ArgumentException>(() => ConfigurationSaveRequest.Parse(body.RootElement));
    }

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
            Assert.Contains("id=\"reboot-dialog\"", page);
            var stylesheet = await http.GetStringAsync("/css/dashboard.css");
            Assert.Contains("#toggle-led[data-state=\"off\"]", stylesheet);
            var script = await http.GetStringAsync("/js/dashboard.js");
            Assert.Contains("const channelRowViews = new Map()", script);
            Assert.NotNull(handler.CookieContainer.GetCookies(http.BaseAddress!)[$"PicoUart.Session.{port}"]);
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
