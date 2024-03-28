using Tekton.API.Application.Multitenancy;

namespace Tekton.API.Host.Controllers.Multitenancy;

public class TenantsController : VersionNeutralApiController
{
    [HttpGet]
    [MustHavePermission(TektonAction.View, TektonResource.Tenants)]
    [OpenApiOperation("Get a list of all tenants.", "")]
    public Task<List<TenantDto>> GetListAsync()
    {
        return Mediator.Send(new GetAllTenantsRequest());
    }

    [HttpGet("{id}")]
    [MustHavePermission(TektonAction.View, TektonResource.Tenants)]
    [OpenApiOperation("Get tenant details.", "")]
    public Task<TenantDto> GetAsync(string id)
    {
        return Mediator.Send(new GetTenantRequest(id));
    }

    [HttpPost]
    [MustHavePermission(TektonAction.Create, TektonResource.Tenants)]
    [OpenApiOperation("Create a new tenant.", "")]
    public Task<string> CreateAsync(CreateTenantRequest request)
    {
        return Mediator.Send(request);
    }

    [HttpPost("{id}/activate")]
    [MustHavePermission(TektonAction.Update, TektonResource.Tenants)]
    [OpenApiOperation("Activate a tenant.", "")]
    [ApiConventionMethod(typeof(TektonApiConventions), nameof(TektonApiConventions.Register))]
    public Task<string> ActivateAsync(string id)
    {
        return Mediator.Send(new ActivateTenantRequest(id));
    }

    [HttpPost("{id}/deactivate")]
    [MustHavePermission(TektonAction.Update, TektonResource.Tenants)]
    [OpenApiOperation("Deactivate a tenant.", "")]
    [ApiConventionMethod(typeof(TektonApiConventions), nameof(TektonApiConventions.Register))]
    public Task<string> DeactivateAsync(string id)
    {
        return Mediator.Send(new DeactivateTenantRequest(id));
    }

    [HttpPost("{id}/upgrade")]
    [MustHavePermission(TektonAction.UpgradeSubscription, TektonResource.Tenants)]
    [OpenApiOperation("Upgrade a tenant's subscription.", "")]
    [ApiConventionMethod(typeof(TektonApiConventions), nameof(TektonApiConventions.Register))]
    public async Task<ActionResult<string>> UpgradeSubscriptionAsync(string id, UpgradeSubscriptionRequest request)
    {
        return id != request.TenantId
            ? BadRequest()
            : Ok(await Mediator.Send(request));
    }
}