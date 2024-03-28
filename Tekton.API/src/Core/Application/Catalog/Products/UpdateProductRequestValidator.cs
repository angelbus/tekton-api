namespace Tekton.API.Application.Catalog.Products;

public class UpdateProductRequestValidator : CustomValidator<UpdateProductRequest>
{
    public UpdateProductRequestValidator(IReadRepository<Product> productRepo, IReadRepository<Brand> brandRepo, IStringLocalizer<UpdateProductRequestValidator> T)
    {
        RuleFor(p => p.Name)
            .NotEmpty()
            .MaximumLength(75)
            .MustAsync(async (product, name, ct) =>
                    await productRepo.FirstOrDefaultAsync(new ProductByNameSpec(name), ct)
                        is not Product existingProduct || existingProduct.Id == product.Id)
                .WithMessage((_, name) => T["Product {0} already Exists.", name]);

        RuleFor(p => p.Price)
            .GreaterThanOrEqualTo(0);

        RuleFor(p => p.Status)
            .GreaterThanOrEqualTo(0)
            .LessThan(2);

        RuleFor(p => p.Stock)
            .GreaterThanOrEqualTo(0);

        RuleFor(p => p.Image);
    }
}