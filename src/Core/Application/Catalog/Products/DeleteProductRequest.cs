using Tekton.API.Application.Common.Caching;
using Tekton.API.Domain.Common.Events;

namespace Tekton.API.Application.Catalog.Products;

public class DeleteProductRequest : IRequest<Guid>
{
    public Guid Id { get; set; }

    public DeleteProductRequest(Guid id) => Id = id;
}

public class DeleteProductRequestHandler : IRequestHandler<DeleteProductRequest, Guid>
{
    private readonly IRepository<Product> _repository;
    private readonly IStringLocalizer _t;
    private readonly ICacheService _cacheService;

    public DeleteProductRequestHandler(IRepository<Product> repository, IStringLocalizer<DeleteProductRequestHandler> localizer, ICacheService cacheService) =>
        (_repository, _t, _cacheService) = (repository, localizer, cacheService);

    public async Task<Guid> Handle(DeleteProductRequest request, CancellationToken cancellationToken)
    {
        var product = await _repository.GetByIdAsync(request.Id, cancellationToken);

        _ = product ?? throw new NotFoundException(_t["Product {0} Not Found."]);

        product.DomainEvents.Add(EntityDeletedEvent.WithEntity(product));

        await _repository.DeleteAsync(product, cancellationToken);

        // The items are cached up to 5', by default. No need to await for this task...
        await _cacheService.RemoveAsync(request.Id.ToString());

        return request.Id;
    }
}