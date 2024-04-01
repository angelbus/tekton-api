using System.Collections.ObjectModel;

namespace Tekton.API.Shared.Authorization;

public static class TektonAction
{
    public const string View = nameof(View);
    public const string Search = nameof(Search);
    public const string Create = nameof(Create);
    public const string Update = nameof(Update);
    public const string Delete = nameof(Delete);
    public const string Export = nameof(Export);
    public const string Generate = nameof(Generate);
    public const string Clean = nameof(Clean);
    public const string UpgradeSubscription = nameof(UpgradeSubscription);
}

public static class TektonResource
{
    public const string Tenants = nameof(Tenants);
    public const string Dashboard = nameof(Dashboard);
    public const string Hangfire = nameof(Hangfire);
    public const string Users = nameof(Users);
    public const string UserRoles = nameof(UserRoles);
    public const string Roles = nameof(Roles);
    public const string RoleClaims = nameof(RoleClaims);
    public const string Products = nameof(Products);
    public const string Brands = nameof(Brands);
}

public static class TektonPermissions
{
    private static readonly TektonPermission[] _all = new TektonPermission[]
    {
        new("View Dashboard", TektonAction.View, TektonResource.Dashboard),
        new("View Hangfire", TektonAction.View, TektonResource.Hangfire),
        new("View Users", TektonAction.View, TektonResource.Users),
        new("Search Users", TektonAction.Search, TektonResource.Users),
        new("Create Users", TektonAction.Create, TektonResource.Users),
        new("Update Users", TektonAction.Update, TektonResource.Users),
        new("Delete Users", TektonAction.Delete, TektonResource.Users),
        new("Export Users", TektonAction.Export, TektonResource.Users),
        new("View UserRoles", TektonAction.View, TektonResource.UserRoles),
        new("Update UserRoles", TektonAction.Update, TektonResource.UserRoles),
        new("View Roles", TektonAction.View, TektonResource.Roles),
        new("Create Roles", TektonAction.Create, TektonResource.Roles),
        new("Update Roles", TektonAction.Update, TektonResource.Roles),
        new("Delete Roles", TektonAction.Delete, TektonResource.Roles),
        new("View RoleClaims", TektonAction.View, TektonResource.RoleClaims),
        new("Update RoleClaims", TektonAction.Update, TektonResource.RoleClaims),
        new("View Products", TektonAction.View, TektonResource.Products, IsBasic: true),
        new("Search Products", TektonAction.Search, TektonResource.Products, IsBasic: true),
        new("Create Products", TektonAction.Create, TektonResource.Products),
        new("Update Products", TektonAction.Update, TektonResource.Products),
        new("Delete Products", TektonAction.Delete, TektonResource.Products),
        new("Export Products", TektonAction.Export, TektonResource.Products),
        new("View Brands", TektonAction.View, TektonResource.Brands, IsBasic: true),
        new("Search Brands", TektonAction.Search, TektonResource.Brands, IsBasic: true),
        new("Create Brands", TektonAction.Create, TektonResource.Brands),
        new("Update Brands", TektonAction.Update, TektonResource.Brands),
        new("Delete Brands", TektonAction.Delete, TektonResource.Brands),
        new("Generate Brands", TektonAction.Generate, TektonResource.Brands),
        new("Clean Brands", TektonAction.Clean, TektonResource.Brands),
        new("View Tenants", TektonAction.View, TektonResource.Tenants, IsRoot: true),
        new("Create Tenants", TektonAction.Create, TektonResource.Tenants, IsRoot: true),
        new("Update Tenants", TektonAction.Update, TektonResource.Tenants, IsRoot: true),
        new("Upgrade Tenant Subscription", TektonAction.UpgradeSubscription, TektonResource.Tenants, IsRoot: true)
    };

    public static IReadOnlyList<TektonPermission> All { get; } = new ReadOnlyCollection<TektonPermission>(_all);
    public static IReadOnlyList<TektonPermission> Root { get; } = new ReadOnlyCollection<TektonPermission>(_all.Where(p => p.IsRoot).ToArray());
    public static IReadOnlyList<TektonPermission> Admin { get; } = new ReadOnlyCollection<TektonPermission>(_all.Where(p => !p.IsRoot).ToArray());
    public static IReadOnlyList<TektonPermission> Basic { get; } = new ReadOnlyCollection<TektonPermission>(_all.Where(p => p.IsBasic).ToArray());
}

public record TektonPermission(string Description, string Action, string Resource, bool IsBasic = false, bool IsRoot = false)
{
    public string Name => NameFor(Action, Resource);
    public static string NameFor(string action, string resource) => $"Permissions.{resource}.{action}";
}
