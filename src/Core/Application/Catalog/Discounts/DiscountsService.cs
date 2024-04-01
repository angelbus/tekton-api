using System;
using System.Net.Http;
using System.Net.Http.Json;
using System.Threading.Tasks;
using Microsoft.Extensions.Configuration;
using Tekton.API.Application.Catalog;
using Tekton.API.Core.Domain.Catalog;

namespace Tekton.API.Core.Application.Catalog.Discounts;

public class DiscountsService : IDiscountsService
{
    private readonly HttpClient _client;
    private readonly ILogger<DiscountsService> _logger;
    private string? _remoteServiceBaseUrl;
    private string? _remoteServiceRequestUrl;

    public DiscountsService(HttpClient client, ILogger<DiscountsService> logger)
    {
        _client = client;
        _logger = logger;

        var tektonConfig = new ConfigurationBuilder().AddJsonFile("Configurations/external.json").Build();

        // Get the external service uri
        GetExternalURL(ref tektonConfig);

        // Get the trailing's external service param uri
        GetExternalTrailingURL(ref tektonConfig);

        _client.BaseAddress = new Uri(_remoteServiceBaseUrl ?? string.Empty);
    }

    public async Task<int> GetDiscountAsync(string id)
    {
        var returnVal = 0;
        if (!string.IsNullOrEmpty(_remoteServiceBaseUrl))
        {
            try
            {
                ProductDiscountDto? discount = await _client.GetFromJsonAsync<ProductDiscountDto>($"{_remoteServiceRequestUrl}{id}");
                returnVal = discount != null ? discount!.Discount : 0;
            }
            catch (HttpRequestException e)
            {
                _logger.LogInformation("The ExternalDiscountsUrl service returned ERROR: " + e.Message);
            }
        }

        return returnVal;
    }

    private void GetExternalURL(ref IConfigurationRoot tektonConfig)
    {
        // Get the external service uri
        _remoteServiceBaseUrl = Environment.GetEnvironmentVariable("ExternalDiscountsUrl")
            ?? tektonConfig.GetValue<string>("DiscountsSettings:DiscountsUrl") ?? string.Empty;

        var loggerMsg = string.IsNullOrEmpty(_remoteServiceBaseUrl)
            ? "The URL ExternalDiscountsUrl does not exist. Discounts will not be applied"
            : "The ExternalDiscountsUrl has been set: " + _remoteServiceBaseUrl;

        _logger.LogInformation(loggerMsg);
    }

    private void GetExternalTrailingURL(ref IConfigurationRoot tektonConfig)
    {
        // Get the trailing's external service param uri
        _remoteServiceRequestUrl = Environment.GetEnvironmentVariable("DiscountsRequestUrl")
            ?? tektonConfig.GetValue<string>("DiscountsSettings:DiscountsRequestUrl");

        var loggerMsg = string.IsNullOrEmpty(_remoteServiceRequestUrl)
            ? "The URL ExternalDiscountsUrl does not exist. Discounts will not be applied"
            : "The ExternalDiscountsUrl has been set: " + _remoteServiceRequestUrl;

        _logger.LogInformation(loggerMsg);
    }
}