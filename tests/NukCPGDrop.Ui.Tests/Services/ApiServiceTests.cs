using Xunit;
using Moq;
using Moq.Protected;
using System.Net;
using System.Net.Http.Json;
using NukCPGDrop.Ui.Services;
using NukCPGDrop.Ui.Models;

namespace NukCPGDrop.Ui.Tests.Services;

public class ApiServiceTests
{
    [Fact]
    public async Task GetStatusAsync_ReturnsStatus()
    {
        var expected = new SystemStatus
        {
            Difficulty = 1,
            DoubleDrop = false,
            DropCount = 5,
            Held = new[] { true, true, true, true, true, true },
            Pca9685Present = true,
            CustomInterval = 2000,
            RangeMin = 300,
            RangeMax = 2000,
        };

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
    public async Task DropAllAsync_ReturnsTrueOnSuccess()
    {
        var http = CreateMockHttpClient(new { });
        var service = new ApiService(http);

        var result = await service.DropAllAsync();
        Assert.True(result);
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

    public MockHttpHandler(HttpStatusCode status, object? content = null)
    {
        _status = status;
        _content = content;
    }

    protected override async Task<HttpResponseMessage> SendAsync(
        HttpRequestMessage request, CancellationToken cancellationToken)
    {
        var response = new HttpResponseMessage(_status);
        if (_content != null)
        {
            response.Content = JsonContent.Create(_content);
        }
        return await Task.FromResult(response);
    }
}
