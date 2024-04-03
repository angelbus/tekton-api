using System;
using System.Net.Http;
using System.Net.Http.Json;
using System.Threading.Tasks;
using Microsoft.Extensions.Configuration;
using Tekton.API.Application.Catalog;
using Tekton.API.Application.Catalog.Products;
using Tekton.API.Application.Common.Caching;
using Tekton.API.Core.Domain.Catalog;

namespace Tekton.API.Core.Application.Catalog.Status;

public class StatusService : IStatusService
{
    private readonly ILogger<StatusService> _logger;
    private readonly ICacheService _cacheService;

    public StatusService(ICacheService cacheService, ILogger<StatusService> logger) => (_logger, _cacheService) = (logger, cacheService);

    public async Task<string> GetStatusNameAsync(string id, int status)
    {
        var itemInCache = await _cacheService.GetAsync<ProductStatusDto>(id);

        return itemInCache == null || string.IsNullOrEmpty(itemInCache.StatusName)
            ? await UpdateStatusNameAsync(id, status)
            : itemInCache.StatusName;
    }

    public async Task<string> UpdateStatusNameAsync(string id, int status)
    {
        var statusName = status == 0 ? "Inactive" : "Active";
        await _cacheService.SetAsync<ProductStatusDto>(id, new ProductStatusDto
        {
            Status = status,
            StatusName = statusName
        });
        _logger.LogInformation("UpdateStatusNameAsync - Status Cache Upated for Id: " + id + " with StatusName: " + statusName);

        return statusName;
    }

    public async Task DeleteStatusNameAsync(string id)
    {
        // The items are cached up to 5', by default. No need to await for this task...
        await _cacheService.RemoveAsync(id);
        _logger.LogInformation("DeleteStatusNameAsync - Status Cache Deleted for Id: " + id);
    }
}