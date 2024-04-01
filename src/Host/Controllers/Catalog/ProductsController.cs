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
    [OpenApiOperation("Search products using available filters.", "This is an asynchronous method that returns only the stored Product data. It DOES NOT RETURN the cacehd data (Status) nor the external data (Discounts); for those cases, use the GET method.")]
    public Task<PaginationResponse<ProductDto>> SearchAsync(SearchProductsRequest request)
    {
        StartLocalTimer();
        var retVal = Mediator.Send(request);
        LogLocalTimer("Products::SearchAsync");
        return retVal;
    }

    [HttpGet("{id:guid}")]
    [MustHavePermission(TektonAction.View, TektonResource.Products)]
    [OpenApiOperation("Get product details.", "As requested, this method returns the interanl Product data, plus the cached 'Satus Name' and the Final Price with its external Dicount.")]
    public Task<ProductDetailsDto> GetAsync(Guid id)
    {
        StartLocalTimer();
        var retVal = Mediator.Send(new GetProductRequest(id));
        LogLocalTimer("Products::GetAsync");
        return retVal;
    }

    [HttpPost]
    [MustHavePermission(TektonAction.Create, TektonResource.Products)]
    [OpenApiOperation("Create a new product.", "This method created a new prodcut and returns the ID, which can be used by the client as the correlation ID.")]
    public Task<Guid> CreateAsync(CreateProductRequest request)
    {
        StartLocalTimer();
        var retVal = Mediator.Send(request);
        LogLocalTimer("Products::CreateAsync");
        return retVal;
    }

    [HttpPut("{id:guid}")]
    [MustHavePermission(TektonAction.Update, TektonResource.Products)]
    [OpenApiOperation("Update a product.", "PUT method to update a profuct. NOTE: The omitted filds will be overwritten with their defaults.")]
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
    [OpenApiOperation("Delete a product.", "This method deletes de product data. It's not a temporary operation.")]
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