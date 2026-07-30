using Xunit;
using Moq;
using Moq.Protected;
using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using NukCPGDrop.Ui.Services;
using NukCPGDrop.Ui.Models;

namespace NukCPGDrop.Ui.Tests.Services;

public class ApiServiceTests
{
    [Fact]
    public async Task GetStatusAsync_ReturnsStatus()
    {
        var expected = new SystemStatus { Difficulty = 1, DropCount = 5, Held = new bool[16], Pca9685Present = true, CustomInterval = 2000, RangeMin = 300, RangeMax = 2000 };
        var http = CreateMockHttpClient(expected);
        var service = new ApiService(http);
        var result = await service.GetStatusAsync();
        Assert.NotNull(result);
        Assert.Equal(5, result!.DropCount);
    }

    [Fact]
    public async Task GetStatusAsync_ReturnsNullOnError()
    {
        var http = new HttpClient(new MockHttpHandler(HttpStatusCode.NotFound));
        var service = new ApiService(http);
        var result = await service.GetStatusAsync();
        Assert.Null(result);
    }

    [Fact]
    public async Task UpdateConfigAsync_PreservesActiveServos()
    {
        string? capturedBody = null;
        var handler = new MockHttpHandler(HttpStatusCode.OK, null, body => capturedBody = body);
        var http = new HttpClient(handler) { BaseAddress = new Uri("http://localhost") };
        var service = new ApiService(http);

        var config = new DropConfiguration { Difficulty = Difficulty.Random, RangeMin = 500, RangeMax = 3000, ActiveServos = 7, SoundEnabled = true, SvStartPos = 10, SvStopPos = 170 };
        await service.UpdateConfigAsync(config);

        Assert.NotNull(capturedBody);
        var json = JsonDocument.Parse(capturedBody!);
        Assert.Equal(7, json.RootElement.GetProperty("active_servos").GetInt32());
        Assert.Equal(500, json.RootElement.GetProperty("range_min").GetInt32());
        Assert.Equal(3000, json.RootElement.GetProperty("range_max").GetInt32());
        Assert.Equal(2, json.RootElement.GetProperty("difficulty").GetInt32());
    }

    [Fact]
    public async Task UpdateConfigAsync_DoesNotLoseFields()
    {
        string? capturedBody = null;
        var handler = new MockHttpHandler(HttpStatusCode.OK, null, body => capturedBody = body);
        var http = new HttpClient(handler) { BaseAddress = new Uri("http://localhost") };
        var service = new ApiService(http);

        // Simulate the FullConfig() pattern — all fields sent together
        var config = new DropConfiguration { Difficulty = Difficulty.Random, RangeMin = 800, RangeMax = 4000, ActiveServos = 7, SoundEnabled = false, SvStartPos = 30, SvStopPos = 150 };
        await service.UpdateConfigAsync(config);

        var json = JsonDocument.Parse(capturedBody!);
        Assert.Equal(7, json.RootElement.GetProperty("active_servos").GetInt32());
        Assert.Equal(800, json.RootElement.GetProperty("range_min").GetInt32());
        Assert.Equal(4000, json.RootElement.GetProperty("range_max").GetInt32());
        Assert.False(json.RootElement.GetProperty("sound_enabled").GetBoolean());
        Assert.Equal(30, json.RootElement.GetProperty("sv_start_pos").GetInt32());
        Assert.Equal(150, json.RootElement.GetProperty("sv_stop_pos").GetInt32());
        Assert.Equal(7, json.RootElement.GetProperty("active_servos").GetInt32());
    }

    private static HttpClient CreateMockHttpClient<T>(T responseContent)
    {
        var handler = new MockHttpHandler(HttpStatusCode.OK, responseContent);
        return new HttpClient(handler) { BaseAddress = new Uri("http://localhost") };
    }
}

public class MockHttpHandler : HttpMessageHandler
{
    private readonly HttpStatusCode _status;
    private readonly object? _content;
    private readonly Action<string>? _onRequest;

    public MockHttpHandler(HttpStatusCode status, object? content = null, Action<string>? onRequest = null)
    {
        _status = status;
        _content = content;
        _onRequest = onRequest;
    }

    protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        if (_onRequest != null && request.Content != null)
        {
            var body = await request.Content.ReadAsStringAsync();
            _onRequest(body);
        }
        var response = new HttpResponseMessage(_status);
        if (_content != null) response.Content = JsonContent.Create(_content);
        return response;
    }
}
