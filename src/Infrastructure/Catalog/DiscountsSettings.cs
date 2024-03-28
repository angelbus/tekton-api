namespace Tekton.API.Infrastructure.Catalog.Discounts;

public class DiscountsSettings
{
    public string AppName { get; set; } = "Tekton.API";
    public string DiscountsUrl { get; set; } = string.Empty;
    public string Token { get; set; } = string.Empty;
    public string UserAgent { get; set; } = string.Empty;
}
