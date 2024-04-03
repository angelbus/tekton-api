namespace Tekton.API.Core.Domain.Catalog;

public interface IStatusService
{
    Task<string> GetStatusNameAsync(string id, int status);
    Task<string> UpdateStatusNameAsync(string id, int status);
    Task DeleteStatusNameAsync(string id);
}