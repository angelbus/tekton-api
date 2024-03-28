namespace Tekton.API.Application.Catalog.Products;

public class CreateProductRequestValidator : CustomValidator<CreateProductRequest>
{
    public CreateProductRequestValidator(IReadRepository<Product> productRepo, IReadRepository<Brand> brandRepo, IStringLocalizer<CreateProductRequestValidator> T)
    {
        RuleFor(p => p.Name)
            .NotEmpty()
            .MaximumLength(75)
            .MustAsync(async (name, ct) => await productRepo.FirstOrDefaultAsync(new ProductByNameSpec(name), ct) is null)
                .WithMessage((_, name) => T["Product {0} already Exists.", name]);

        RuleFor(p => p.Price)
            .GreaterThanOrEqualTo(0);

        RuleFor(p => p.Status)
            .GreaterThanOrEqualTo(0)
            .LessThan(2);

        RuleFor(p => p.Stock)
            .GreaterThanOrEqualTo(0);

        RuleFor(p => p.Image);

        /* Not needed for Tekton Challenge
        RuleFor(p => p.Brand)
            .NotEmpty()
            .MustAsync(async (id, ct) => await brandRepo.GetByIdAsync(id, ct) is not null)
                .WithMessage((_, id) => T["Brand {0} Not Found.", id]); */
    }
}