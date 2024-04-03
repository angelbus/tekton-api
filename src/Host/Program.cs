using Tekton.API.Application;
using Tekton.API.Host.Configurations;
using Tekton.API.Host.Controllers;
using Tekton.API.Infrastructure;
using Tekton.API.Infrastructure.Common;
using Tekton.API.Infrastructure.Logging.Serilog;
using Tekton.API.Core.Application.Catalog.Discounts;
using Tekton.API.Core.Application.Catalog.Status;
using Tekton.API.Core.Domain.Catalog;
using Serilog;
using Serilog.Formatting.Compact;

[assembly: ApiConventionType(typeof(TektonApiConventions))]

StaticLogger.EnsureInitialized();
Log.Information("Server Booting Up...");
try
{
    var builder = WebApplication.CreateBuilder(args);
    builder.AddConfigurations().RegisterSerilog();
    builder.Services.AddControllers();
    builder.Services.AddInfrastructure(builder.Configuration);
    builder.Services.AddApplication();

    builder.Services.AddHttpClient<IDiscountsService, DiscountsService>(client =>
    {
        var settingshUrl = Environment.GetEnvironmentVariable("DiscountsUrl");
        if (!string.IsNullOrEmpty(settingshUrl))
        {
            Console.WriteLine("Environment variable for DiscountsUrl: " + settingshUrl);
        }
        else
        {
            settingshUrl = builder.Configuration["DiscountsUrl"];
            Console.WriteLine("Environment variable for DiscountsUrl does not exist");
        }

        client.BaseAddress = new Uri(settingshUrl ?? string.Empty);
    })
    .ConfigurePrimaryHttpMessageHandler(() =>
    {
        return new SocketsHttpHandler()
        {
            PooledConnectionLifetime = TimeSpan.FromMinutes(15)
        };
    })
    .SetHandlerLifetime(Timeout.InfiniteTimeSpan);
    builder.Services.AddSingleton<IDiscountsService, DiscountsService>();
    builder.Services.AddSingleton<IStatusService, StatusService>();

    var app = builder.Build();

    await app.Services.InitializeDatabasesAsync();

    app.UseInfrastructure(builder.Configuration);
    app.MapEndpoints();
    app.Run();
}
catch (Exception ex) when (!ex.GetType().Name.Equals("StopTheHostException", StringComparison.Ordinal))
{
    StaticLogger.EnsureInitialized();
    Log.Fatal(ex, "Unhandled exception");
}
finally
{
    StaticLogger.EnsureInitialized();
    Log.Information("Server Shutting down...");
    Log.CloseAndFlush();
}