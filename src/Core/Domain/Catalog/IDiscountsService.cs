namespace Tekton.API.Core.Domain.Catalog;

public interface IDiscountsService
{
    Task<int> GetDiscountAsync(string id);
}