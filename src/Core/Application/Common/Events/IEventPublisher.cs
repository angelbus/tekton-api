using Tekton.API.Shared.Events;

namespace Tekton.API.Application.Common.Events;

public interface IEventPublisher : ITransientService
{
    Task PublishAsync(IEvent @event);
}