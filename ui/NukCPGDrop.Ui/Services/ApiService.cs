using System.Net.Http.Json;
using NukCPGDrop.Ui.Models;

namespace NukCPGDrop.Ui.Services;

public class ApiService
{
    private readonly HttpClient _http;

    public ApiService(HttpClient http)
    {
        _http = http;
    }

    public async Task<SystemStatus?> GetStatusAsync()
    {
        try
        {
            return await _http.GetFromJsonAsync<SystemStatus>("/api/status");
        }
        catch
        {
            return null;
        }
    }

    public async Task<bool> DropAllAsync()
    {
        try
        {
            var response = await _http.PostAsync("/api/drop", null);
            return response.IsSuccessStatusCode;
        }
        catch { return false; }
    }

    public async Task<bool> DropOneAsync(int id)
    {
        try
        {
            var cmd = new DropCommand { Id = id };
            var response = await _http.PostAsJsonAsync("/api/drop", cmd);
            return response.IsSuccessStatusCode;
        }
        catch { return false; }
    }

    public async Task<bool> ResetAllAsync()
    {
        try
        {
            var response = await _http.PostAsync("/api/reset", null);
            return response.IsSuccessStatusCode;
        }
        catch { return false; }
    }

    public async Task<bool> UpdateConfigAsync(DropConfiguration config)
    {
        try
        {
            var response = await _http.PostAsJsonAsync("/api/config", new
            {
                difficulty = (int)config.Difficulty,
                double_drop = config.DoubleDrop,
                custom_interval = config.CustomInterval,
                range_min = config.RangeMin,
                range_max = config.RangeMax,
            });
            return response.IsSuccessStatusCode;
        }
        catch { return false; }
    }
}
