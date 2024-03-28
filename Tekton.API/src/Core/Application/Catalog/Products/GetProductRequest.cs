using Tekton.API.Application.Common.Caching;
using Tekton.API.Core.Domain.Catalog;

namespace Tekton.API.Application.Catalog.Products;

public class GetProductRequest : IRequest<ProductDetailsDto>
{
    public Guid Id { get; set; }

    public GetProductRequest(Guid id) => Id = id;
}

public class GetProductRequestHandler : IRequestHandler<GetProductRequest, ProductDetailsDto>
{
    private readonly IRepository<Product> _repository;
    private readonly IStringLocalizer _t;
    private readonly ICacheService _cacheService;
    private readonly IDiscountsService _discountsService;

    public GetProductRequestHandler(IRepository<Product> repository, IStringLocalizer<GetProductRequestHandler> localizer, ICacheService cacheService, IDiscountsService discountsService) =>
        (_repository, _t, _cacheService, _discountsService) = (repository, localizer, cacheService, discountsService);

    public async Task<ProductDetailsDto> Handle(GetProductRequest request, CancellationToken cancellationToken)
    {
        var productDetails = await _repository.FirstOrDefaultAsync(
            (ISpecification<Product, ProductDetailsDto>)new ProductByIdWithBrandSpec(request.Id), cancellationToken)
        ?? throw new NotFoundException(_t["Product {0} Not Found.", request.Id]);

        var itemInCache = await _cacheService.GetAsync<ProductStatusDto>(productDetails.Id.ToString());
        productDetails.StatusName = (itemInCache == null || string.IsNullOrEmpty(itemInCache.StatusName)) ? "Inactive" : itemInCache.StatusName;

        productDetails.Discount = await _discountsService.GetDiscountAsync(productDetails.Id.ToString());
        AssignProductDiscount(ref productDetails);

        return productDetails;
    }

    private void AssignProductDiscount(ref ProductDetailsDto product)
    {
        Console.WriteLine("Product Discount: " + product.Discount);
        product.FinalPrice = product.Price * (100 - product.Discount) / 100;
        Console.WriteLine("Product FinalPrice: " + product.FinalPrice);
    }
}