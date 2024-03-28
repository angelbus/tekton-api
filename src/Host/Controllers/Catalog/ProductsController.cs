using System.Diagnostics;
using Tekton.API.Application.Catalog.Products;

namespace Tekton.API.Host.Controllers.Catalog;

public class ProductsController : VersionedApiController
{
    private readonly ILogger<ProductsController> _logger;

    private readonly System.Diagnostics.Stopwatch _watch = new Stopwatch();

    public ProductsController(ILogger<ProductsController> logger) => _logger = logger;

    [HttpPost("search")]
    [MustHavePermission(TektonAction.Search, TektonResource.Products)]
    [OpenApiOperation("Search products using available filters.", "")]
    public Task<PaginationResponse<ProductDto>> SearchAsync(SearchProductsRequest request)
    {
        StartLocalTimer();
        var retVal = Mediator.Send(request);
        LogLocalTimer("Products::SearchAsync");
        return retVal;
    }

    [HttpGet("{id:guid}")]
    [MustHavePermission(TektonAction.View, TektonResource.Products)]
    [OpenApiOperation("Get product details.", "")]
    public Task<ProductDetailsDto> GetAsync(Guid id)
    {
        StartLocalTimer();
        var retVal = Mediator.Send(new GetProductRequest(id));
        LogLocalTimer("Products::GetAsync");
        return retVal;
    }

    [HttpPost]
    [MustHavePermission(TektonAction.Create, TektonResource.Products)]
    [OpenApiOperation("Create a new product.", "")]
    public Task<Guid> CreateAsync(CreateProductRequest request)
    {
        StartLocalTimer();
        var retVal = Mediator.Send(request);
        LogLocalTimer("Products::CreateAsync");
        return retVal;
    }

    [HttpPut("{id:guid}")]
    [MustHavePermission(TektonAction.Update, TektonResource.Products)]
    [OpenApiOperation("Update a product.", "")]
    public async Task<ActionResult<Guid>> UpdateAsync(UpdateProductRequest request, Guid id)
    {
        StartLocalTimer();
        ActionResult<Guid> retVal = id != request.Id
            ? BadRequest()
            : Ok(await Mediator.Send(request));
        LogLocalTimer("Products::UpdateAsync");
        return retVal;
    }

    [HttpDelete("{id:guid}")]
    [MustHavePermission(TektonAction.Delete, TektonResource.Products)]
    [OpenApiOperation("Delete a product.", "")]
    public Task<Guid> DeleteAsync(Guid id)
    {
        StartLocalTimer();
        var retVal = Mediator.Send(new DeleteProductRequest(id));
        LogLocalTimer("Products::DeleteAsync");
        return retVal;
    }

    private void StartLocalTimer()
    {
        // Start the Timer using Stopwatch
        _watch.Start();
    }

    private void LogLocalTimer(string message)
    {
        // Stop the timer information and calculate the time
        _watch.Stop();
        _logger.LogInformation("SearchAsync - Request Action Completed: " + _watch.ElapsedMilliseconds + " Milliseconds.", message);
    }
}