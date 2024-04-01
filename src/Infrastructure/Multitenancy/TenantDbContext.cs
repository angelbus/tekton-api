using Finbuckle.MultiTenant.Stores;
using Tekton.API.Infrastructure.Persistence.Configuration;
using Microsoft.EntityFrameworkCore;

namespace Tekton.API.Infrastructure.Multitenancy;

public class TenantDbContext : EFCoreStoreDbContext<TektonTenantInfo>
{
    public TenantDbContext(DbContextOptions<TenantDbContext> options)
        : base(options)
    {
        AppContext.SetSwitch("Npgsql.EnableLegacyTimestampBehavior", true);
    }

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        base.OnModelCreating(modelBuilder);

        modelBuilder.Entity<TektonTenantInfo>().ToTable("Tenants", SchemaNames.MultiTenancy);
    }
}