// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using PicoUart.Dashboard;
using PicoUart.Hid;
using PicoUart.Wire;
using Xunit;

namespace PicoUart.Tests;

[Trait("Category", "Unit")]
public sealed class DashboardHostTests
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
        using var http = new HttpClient(handler)
        {
            BaseAddress = new($"http://127.0.0.1:{port}"),
            Timeout = TimeSpan.FromSeconds(5)
        };
        handler.CookieContainer.Add(http.BaseAddress!, new Cookie($"PicoUart.Session.{port}", "legacy-cookie"));
        handler.CookieContainer.Add(http.BaseAddress!, new Cookie($"PicoUart.Python.Session.{port}", "python-cookie"));
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
            Assert.NotNull(handler.CookieContainer.GetCookies(http.BaseAddress!)[$"PicoUart.DotNet.Session.{port}"]);
            var token = Regex.Match(page, "[A-F0-9]{64}").Value;
            Assert.Equal(64, token.Length);
            const string valid = "{\"profile\":0,\"unsafe_overclock\":false}";
            await AssertResponse(valid, 403);
            await AssertPostError("/api/actions/toggle-led?board=unknown", 403);
            http.DefaultRequestHeaders.Add("X-CSRF-Token", "invalid");
            await AssertResponse(valid, 403);
            http.DefaultRequestHeaders.Remove("X-CSRF-Token");
            http.DefaultRequestHeaders.Add("X-CSRF-Token", token);
            await AssertResponse(valid, 415, "text/plain");
            await AssertResponse("{", 400);
            await AssertResponse("{\"profile\":1,\"unsafe_overclock\":false}", 400);
            await AssertResponse("{\"profile\":0,\"unsafe_overclock\":false,\"extra\":0}", 400);
            await AssertResponse(valid, 409);
            await AssertResponse(valid.PadRight(1024), 409);
            using (var oversizedContent = new StringContent(new string('x', 2048), Encoding.UTF8, "application/json"))
            using (var oversizedResponse = await http.PostAsync("/api/configuration?board=unknown", oversizedContent))
            {
                Assert.Equal(413, (int)oversizedResponse.StatusCode);
                using var body = JsonDocument.Parse(await oversizedResponse.Content.ReadAsStringAsync());
                Assert.Contains("1024 bytes", body.RootElement.GetProperty("error").GetString());
            }
            using (var chunkedContent = new ChunkedContent(Encoding.UTF8.GetBytes(new string('x', 2048))))
            {
                chunkedContent.Headers.ContentType = new("application/json");
                Assert.Null(chunkedContent.Headers.ContentLength);
                using var chunkedResponse =
                    await http.PostAsync("/api/configuration?board=unknown", chunkedContent);
                Assert.Equal(413, (int)chunkedResponse.StatusCode);
            }

            await AssertGetError("/api/status?board=unknown", 409);
            await AssertGetError("/api/diagnostics?board=unknown", 409);
            await AssertPostError("/api/actions/toggle-led?board=unknown", 409);
            await AssertPostError("/api/actions/unknown?board=unknown", 404);
            using var snapshot = JsonDocument.Parse(await http.GetStringAsync("/api/status"));
            Assert.Equal(JsonValueKind.Null, snapshot.RootElement.GetProperty("configuration_save").ValueKind);
            Assert.Equal(JsonValueKind.Null, snapshot.RootElement.GetProperty("configuration_save_error").ValueKind);
            Assert.False(snapshot.RootElement.GetProperty("configuration_save_unconfirmed").GetBoolean());
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
            if (expectedStatus == 409)
                Assert.False(body.RootElement.GetProperty("outcome_unconfirmed").GetBoolean());
        }

        async Task AssertGetError(string path, int expectedStatus)
        {
            using var response = await http.GetAsync(path);
            Assert.Equal(expectedStatus, (int)response.StatusCode);
            using var body = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            Assert.False(string.IsNullOrEmpty(body.RootElement.GetProperty("error").GetString()));
        }

        async Task AssertPostError(string path, int expectedStatus)
        {
            using var content = new StringContent("{}", Encoding.UTF8, "application/json");
            using var response = await http.PostAsync(path, content);
            Assert.Equal(expectedStatus, (int)response.StatusCode);
            using var body = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            Assert.False(string.IsNullOrEmpty(body.RootElement.GetProperty("error").GetString()));
        }
    }

    [Fact]
    public async Task DashboardRoutesReturnSuccessfulActionAndSaveResults()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        var dashboard = new FakeDashboardBoards();
        await using var app = await DashboardHost.CreateApplicationAsync(port, dashboard);
        await app.StartAsync();
        using var handler = new HttpClientHandler { CookieContainer = new CookieContainer() };
        using var http = new HttpClient(handler)
        {
            BaseAddress = new($"http://127.0.0.1:{port}"),
            Timeout = TimeSpan.FromSeconds(5)
        };
        try
        {
            var page = await http.GetStringAsync("/");
            var token = Regex.Match(page, "[A-F0-9]{64}").Value;
            Assert.Equal(64, token.Length);
            http.DefaultRequestHeaders.Add("X-CSRF-Token", token);
            const string boardId = "test-board";

            using var actionContent = new StringContent("{}", Encoding.UTF8, "application/json");
            using var actionResponse = await http.PostAsync($"/api/actions/toggle-led?board={boardId}", actionContent);
            Assert.Equal(HttpStatusCode.OK, actionResponse.StatusCode);
            using var action = JsonDocument.Parse(await actionResponse.Content.ReadAsStringAsync());
            Assert.Equal((byte)1, action.RootElement.GetProperty("action").GetByte());
            Assert.Equal((byte)2, action.RootElement.GetProperty("status").GetByte());
            Assert.Equal(boardId, dashboard.LastBoardId);
            Assert.True(dashboard.LastActionToken.CanBeCanceled);

            using var saveContent = new StringContent(
                "{\"profile\":0,\"unsafe_overclock\":false}", Encoding.UTF8, "application/json");
            using var saveResponse = await http.PostAsync($"/api/configuration?board={boardId}", saveContent);
            Assert.Equal(HttpStatusCode.OK, saveResponse.StatusCode);
            using var save = JsonDocument.Parse(await saveResponse.Content.ReadAsStringAsync());
            Assert.Equal((byte)4, save.RootElement.GetProperty("action").GetByte());
            Assert.Equal((byte)2, save.RootElement.GetProperty("status").GetByte());
            Assert.Equal(boardId, dashboard.LastBoardId);
            Assert.Equal((uint)0, dashboard.SavedProfile);
            Assert.True(dashboard.LastSaveToken.CanBeCanceled);

            dashboard.ActionError = new ControlOutcomeUnconfirmedException(
                "command outcome is unconfirmed; do not retry; reconnect and verify board state",
                new IOException("private native detail"));
            using var uncertainActionContent = new StringContent("{}", Encoding.UTF8, "application/json");
            using var uncertainActionResponse =
                await http.PostAsync($"/api/actions/toggle-led?board={boardId}", uncertainActionContent);
            Assert.Equal(HttpStatusCode.ServiceUnavailable, uncertainActionResponse.StatusCode);
            var uncertainActionBody = await uncertainActionResponse.Content.ReadAsStringAsync();
            using (var body = JsonDocument.Parse(uncertainActionBody))
            {
                Assert.Contains("outcome is unconfirmed", body.RootElement.GetProperty("error").GetString());
                Assert.True(body.RootElement.GetProperty("outcome_unconfirmed").GetBoolean());
                Assert.DoesNotContain("private native detail", uncertainActionBody);
            }

            dashboard.SaveError = new ControlOutcomeUnconfirmedException(
                "command outcome is unconfirmed; do not retry; reconnect and verify board state",
                new IOException("private native detail"));
            using var uncertainSaveContent = new StringContent(
                "{\"profile\":0,\"unsafe_overclock\":false}", Encoding.UTF8, "application/json");
            using var uncertainSaveResponse = await http.PostAsync(
                $"/api/configuration?board={boardId}", uncertainSaveContent);
            Assert.Equal(HttpStatusCode.ServiceUnavailable, uncertainSaveResponse.StatusCode);
            var uncertainSaveBody = await uncertainSaveResponse.Content.ReadAsStringAsync();
            using (var body = JsonDocument.Parse(uncertainSaveBody))
            {
                Assert.Contains("outcome is unconfirmed", body.RootElement.GetProperty("error").GetString());
                Assert.True(body.RootElement.GetProperty("outcome_unconfirmed").GetBoolean());
                Assert.DoesNotContain("private native detail", uncertainSaveBody);
            }

            using var status = JsonDocument.Parse(await http.GetStringAsync($"/api/status?board={boardId}"));
            Assert.Equal((byte)2, status.RootElement.GetProperty("configuration_save")
                .GetProperty("status").GetByte());
            Assert.Equal((uint)0, status.RootElement.GetProperty("configuration_save")
                .GetProperty("profile").GetUInt32());
        }
        finally
        {
            await app.StopAsync();
        }
    }

    [Theory]
    [InlineData(false, HttpStatusCode.ServiceUnavailable)]
    [InlineData(true, HttpStatusCode.GatewayTimeout)]
    public async Task DiagnosticsRouteReturnsAnExplicitDeviceFailure(bool timeout, HttpStatusCode expected)
    {
        var dashboard = new FakeDashboardBoards
        {
            DiagnosticsError = timeout
                ? new TimeoutException("diagnostics timed out")
                : new IOException("diagnostics acquisition failed")
        };
        await using var app = await DashboardHost.CreateApplicationAsync(0, dashboard);
        await app.StartAsync();
        using var http = new HttpClient { BaseAddress = new(app.Urls.Single()) };
        try
        {
            using var response = await http.GetAsync("/api/diagnostics?board=test-board");
            Assert.Equal(expected, response.StatusCode);
            using var body = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            Assert.Equal(dashboard.DiagnosticsError.Message, body.RootElement.GetProperty("error").GetString());
        }
        finally { await app.StopAsync(); }
    }

    private sealed class FakeDashboardBoards : IDashboardBoards
    {
        private readonly DashboardSnapshot snapshot = new();

        public string? LastBoardId { get; private set; }
        public CancellationToken LastActionToken { get; private set; }
        public CancellationToken LastSaveToken { get; private set; }
        public uint? SavedProfile { get; private set; }
        public Exception? ActionError { get; set; }
        public Exception? SaveError { get; set; }
        public Exception? DiagnosticsError { get; set; }

        public DashboardBoardList Boards() => new([], null, []);

        public string SnapshotJson(string? boardId)
        {
            LastBoardId = boardId;
            return JsonSerializer.Serialize(snapshot, Protocol.JsonOptions);
        }

        public BoardControlStatus Action(
            string? boardId, string action, CancellationToken cancellationToken = default)
        {
            LastBoardId = boardId;
            LastActionToken = cancellationToken;
            if (ActionError is not null) throw ActionError;
            snapshot.ApplyAction(new(action == "toggle-led" ? (byte)1 : (byte)3, 2, 0));
            return snapshot.Control!;
        }

        public BoardControlStatus SaveConfiguration(
            string? boardId, uint profile, bool unsafeOverclock, CancellationToken cancellationToken = default)
        {
            LastBoardId = boardId;
            LastSaveToken = cancellationToken;
            if (SaveError is not null) throw SaveError;
            SavedProfile = profile;
            snapshot.ApplyAction(new(4, 2, 0));
            snapshot.ConfigurationSave = new(2, 0, profile);
            snapshot.ConfigurationSaveError = null;
            return snapshot.Control!;
        }

        public string DiagnosticsJson(string? boardId)
        {
            if (DiagnosticsError is not null) throw DiagnosticsError;
            return "[]";
        }
    }

    private sealed class ChunkedContent(byte[] payload) : HttpContent
    {
        protected override bool TryComputeLength(out long length)
        {
            length = 0;
            return false;
        }

        protected override Task SerializeToStreamAsync(Stream stream, TransportContext? context) =>
            stream.WriteAsync(payload).AsTask();
    }
}
