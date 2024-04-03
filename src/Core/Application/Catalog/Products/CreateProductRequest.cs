using Tekton.API.Core.Domain.Catalog;
using Tekton.API.Domain.Common.Events;

namespace Tekton.API.Application.Catalog.Products;

public class CreateProductRequest : IRequest<Guid>
{
    public string Name { get; set; } = default!;
    public string? Description { get; set; }
    public decimal Price { get; set; }
    public string? Brand { get; set; }
    public FileUploadRequest? Image { get; set; }
    public int Status { get; set; }
    public int Stock { get; set; }
}

public class CreateProductRequestHandler : IRequestHandler<CreateProductRequest, Guid>
{
    private readonly IRepository<Product> _repository;
    private readonly IFileStorageService _file;
    private readonly IStatusService _statusService;

    public CreateProductRequestHandler(IRepository<Product> repository, IFileStorageService file, IStatusService statusService) =>
        (_repository, _file, _statusService) = (repository, file, statusService);

    public async Task<Guid> Handle(CreateProductRequest request, CancellationToken cancellationToken)
    {
        string productImagePath = await _file.UploadAsync<Product>(request.Image, FileType.Image, cancellationToken);

        var product = new Product(request.Name, request.Description, request.Price, request.Brand, productImagePath, request.Status, request.Stock);

        // Add Domain Events to be raised after the commit
        product.DomainEvents.Add(EntityCreatedEvent.WithEntity(product));

        await _repository.AddAsync(product, cancellationToken);

        await _statusService.UpdateStatusNameAsync(product.Id.ToString(), product.Status);

        return product.Id;
    }
}