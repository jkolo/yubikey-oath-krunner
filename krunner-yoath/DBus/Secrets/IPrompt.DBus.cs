using System.Runtime.CompilerServices;
using Tmds.DBus;

[assembly: InternalsVisibleTo(Tmds.DBus.Connection.DynamicAssemblyName)]
namespace KRunner.YOath.DBus.Secrets
{
    [DBusInterface("org.freedesktop.Secret.Prompt")]
    interface IPrompt : IDBusObject
    {
        Task PromptAsync(string WindowId);
        Task DismissAsync();
        Task<IDisposable> WatchCompletedAsync(Action<(bool dismissed, object result)> handler, Action<Exception>? onError = default);
    }
}
