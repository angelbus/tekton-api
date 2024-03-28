namespace Tekton.API.Application.Catalog.Products;

public class ProductStatusDto : IDto
{
    public int Status { get; set; }
    public string StatusName { get; set; } = default!;
}