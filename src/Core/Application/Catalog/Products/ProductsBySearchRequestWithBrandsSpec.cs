namespace Tekton.API.Application.Catalog.Products;

public class ProductsBySearchRequestWithBrandsSpec : EntitiesByPaginationFilterSpec<Product, ProductDto>
{
    public ProductsBySearchRequestWithBrandsSpec(SearchProductsRequest request)
        : base(request) =>
        Query
            .OrderBy(c => c.Name, !request.HasOrderBy())
            .Where(p => p.Brand.Equals(request.Brand), !string.IsNullOrEmpty(request.Brand))
            .Where(p => p.Name.Equals(request.Name), !string.IsNullOrEmpty(request.Name))
            .Where(p => p.Description.Equals(request.Description), !string.IsNullOrEmpty(request.Description))
            .Where(p => p.Price >= request.MinimumPrice!.Value, request.MinimumPrice.HasValue)
            .Where(p => p.Price <= request.MaximumPrice!.Value, request.MaximumPrice.HasValue);
}