namespace Tekton.API.Domain.Catalog;

public class Product : AuditableEntity, IAggregateRoot
{
    public string Name { get; private set; } = default!;
    public string? Description { get; private set; }
    public decimal Price { get; private set; }
    public string? ImagePath { get; private set; }
    public string? Brand { get; private set; } = default!;
    public int Status { get; private set; }
    public int Stock { get; private set; }

    public Product()
    {
        // Only needed for working with dapper (See GetProductViaDapperRequest)
        // If you're not using dapper it's better to remove this constructor.
    }

    public Product(string name, string? description, decimal price, string? brandName, string? imagePath, int status, int stock)
    {
        Name = name;
        Description = description;
        Price = price;
        ImagePath = imagePath;
        Brand = brandName;
        Status = status;
        Stock = stock;
    }

    public Product Update(string? name, string? description, decimal? price, string? brandName, string? imagePath, int status, int stock)
    {
        if (name is not null && Name?.Equals(name) is not true) Name = name;
        if (description is not null && Description?.Equals(description) is not true) Description = description;
        if (price.HasValue && Price != price) Price = price.Value;
        if (brandName is not null && Brand?.Equals(brandName) is not true) Brand = brandName;
        if (imagePath is not null && ImagePath?.Equals(imagePath) is not true) ImagePath = imagePath;
        if (Status != status) Status = status;
        if (Stock != stock) Stock = stock;
        return this;
    }

    public Product ClearImagePath()
    {
        ImagePath = string.Empty;
        return this;
    }
}