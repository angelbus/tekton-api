using Tekton.API.Application.Catalog.Brands;

namespace Tekton.API.Application.Catalog.Products;

public class ProductDetailsDto : IDto
{
    public Guid Id { get; set; }
    public string Name { get; set; } = default!;
    public string? Description { get; set; }
    public decimal Price { get; set; }
    public string? ImagePath { get; set; }
    public string Brand { get; set; } = default!;
    public string StatusName { get; set; } = default!;
    public int Stock { get; set; }
    public int Discount { get; set; }
    public decimal FinalPrice { get; set; }
}