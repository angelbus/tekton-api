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
    private readonly string _remoteServiceBaseUrl;
    private readonly ILogger<DiscountsService> _logger;

    public DiscountsService(HttpClient client, ILogger<DiscountsService> logger)
    {
        _client = client;
        _logger = logger;

        _remoteServiceBaseUrl = Environment.GetEnvironmentVariable("ExternalDiscountsUrl") ?? string.Empty;
        if (string.IsNullOrEmpty(_remoteServiceBaseUrl))
        {
            var myConfig = new ConfigurationBuilder().AddJsonFile("Configurations/external.json").Build();
            _remoteServiceBaseUrl = myConfig.GetValue<string>("DiscountsSettings:DiscountsUrl") ?? string.Empty;
        }

        if (string.IsNullOrEmpty(_remoteServiceBaseUrl))
            _logger.LogInformation("The URL ExternalDiscountsUrl does not exist. Discounts will not be applied");
        else
            _logger.LogInformation("The ExternalDiscountsUrl has been set: " + _remoteServiceBaseUrl);

        _client.BaseAddress = new Uri(_remoteServiceBaseUrl);
    }

    public async Task<int> GetDiscountAsync(string id)
    {
        var returnVal = 0;
        if (!string.IsNullOrEmpty(_remoteServiceBaseUrl))
        {
            ProductDiscountDto? discount = await _client.GetFromJsonAsync<ProductDiscountDto>($"products/{id}");
            returnVal = discount != null ? discount!.Discount : 0;
        }

        return returnVal;
    }
}