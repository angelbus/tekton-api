using Tekton.API.Shared.Multitenancy;

namespace Tekton.API.Infrastructure.OpenApi;

public class TenantIdHeaderAttribute : SwaggerHeaderAttribute
{
    public TenantIdHeaderAttribute()
        : base(
            MultitenancyConstants.TenantIdName,
            "Input your tenant Id to access this API -\r\nTesting email: \"admin@root.com\"\r\nTesting password :\"123Pa$$word!\"",
            "root",
            true)
    {
    }
}
