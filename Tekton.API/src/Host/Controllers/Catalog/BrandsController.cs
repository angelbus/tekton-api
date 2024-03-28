using Tekton.API.Application.Catalog.Brands;

namespace Tekton.API.Host.Controllers.Catalog;

public class BrandsController : VersionedApiController
{
    [HttpPost("search")]
    [MustHavePermission(TektonAction.Search, TektonResource.Brands)]
    [OpenApiOperation("Search brands using available filters.", "")]
    public Task<PaginationResponse<BrandDto>> SearchAsync(SearchBrandsRequest request)
    {
        return Mediator.Send(request);
    }

    [HttpGet("{id:guid}")]
    [MustHavePermission(TektonAction.View, TektonResource.Brands)]
    [OpenApiOperation("Get brand details.", "")]
    public Task<BrandDto> GetAsync(Guid id)
    {
        return Mediator.Send(new GetBrandRequest(id));
    }

    [HttpPost]
    [MustHavePermission(TektonAction.Create, TektonResource.Brands)]
    [OpenApiOperation("Create a new brand.", "")]
    public Task<Guid> CreateAsync(CreateBrandRequest request)
    {
        return Mediator.Send(request);
    }

    [HttpPut("{id:guid}")]
    [MustHavePermission(TektonAction.Update, TektonResource.Brands)]
    [OpenApiOperation("Update a brand.", "")]
    public async Task<ActionResult<Guid>> UpdateAsync(UpdateBrandRequest request, Guid id)
    {
        return id != request.Id
            ? BadRequest()
            : Ok(await Mediator.Send(request));
    }

    [HttpDelete("{id:guid}")]
    [MustHavePermission(TektonAction.Delete, TektonResource.Brands)]
    [OpenApiOperation("Delete a brand.", "")]
    public Task<Guid> DeleteAsync(Guid id)
    {
        return Mediator.Send(new DeleteBrandRequest(id));
    }

    [HttpPost("generate-random")]
    [MustHavePermission(TektonAction.Generate, TektonResource.Brands)]
    [OpenApiOperation("Generate a number of random brands.", "")]
    public Task<string> GenerateRandomAsync(GenerateRandomBrandRequest request)
    {
        return Mediator.Send(request);
    }

    [HttpDelete("delete-random")]
    [MustHavePermission(TektonAction.Clean, TektonResource.Brands)]
    [OpenApiOperation("Delete the brands generated with the generate-random call.", "")]
    [ApiConventionMethod(typeof(TektonApiConventions), nameof(TektonApiConventions.Search))]
    public Task<string> DeleteRandomAsync()
    {
        return Mediator.Send(new DeleteRandomBrandRequest());
    }
}