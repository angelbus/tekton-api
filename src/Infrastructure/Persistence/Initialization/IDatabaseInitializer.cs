using Tekton.API.Infrastructure.Multitenancy;

namespace Tekton.API.Infrastructure.Persistence.Initialization;

internal interface IDatabaseInitializer
{
    Task InitializeDatabasesAsync(CancellationToken cancellationToken);
    Task InitializeApplicationDbForTenantAsync(TektonTenantInfo tenant, CancellationToken cancellationToken);
}