using Tekton.API.Shared.Authorization;
using Microsoft.AspNetCore.Authorization;

namespace Tekton.API.Infrastructure.Auth.Permissions;

public class MustHavePermissionAttribute : AuthorizeAttribute
{
    public MustHavePermissionAttribute(string action, string resource) =>
        Policy = TektonPermission.NameFor(action, resource);
}