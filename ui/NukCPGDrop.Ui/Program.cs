using Microsoft.AspNetCore.Components.Web;
using Microsoft.AspNetCore.Components.WebAssembly.Hosting;
using NukCPGDrop.Ui;
using NukCPGDrop.Ui.Services;

var builder = WebAssemblyHostBuilder.CreateDefault(args);
builder.RootComponents.Add<App>("#app");
builder.RootComponents.Add<HeadOutlet>("head::after");

var apiBase = "http://192.168.4.1/";
#if DEBUG
var baseFromEnv = builder.HostEnvironment.BaseAddress;
if (baseFromEnv.StartsWith("http://") || baseFromEnv.StartsWith("https://"))
    apiBase = baseFromEnv;
#endif
builder.Services.AddScoped(sp => new HttpClient { BaseAddress = new Uri(apiBase) });
builder.Services.AddScoped<ApiService>();
builder.Services.AddScoped<StateService>();

await builder.Build().RunAsync();
