using System.Runtime.CompilerServices;
using Tmds.DBus;

#nullable disable
[assembly: InternalsVisibleTo(Tmds.DBus.Connection.DynamicAssemblyName)]
namespace KRunner.YOath
{
    [DBusInterface("org.freedesktop.Notifications")]
    public interface INotifications : IDBusObject
    {
        Task<uint> NotifyAsync(string AppName, uint ReplacesId, string AppIcon, string Summary, string Body, string[] Actions, IDictionary<string, object> Hints, int Timeout);
        Task CloseNotificationAsync(uint Id);
        Task<string[]> GetCapabilitiesAsync();
        Task<(string name, string vendor, string version, string specVersion)> GetServerInformationAsync();
        Task<uint> InhibitAsync(string DesktopEntry, string Reason, IDictionary<string, object> Hints);
        Task UnInhibitAsync(uint arg0);
        Task<IDisposable> WatchNotificationClosedAsync(Action<(uint id, uint reason)> handler, Action<Exception> onError = null);
        Task<IDisposable> WatchActionInvokedAsync(Action<(uint id, string actionKey)> handler, Action<Exception> onError = null);
        Task<IDisposable> WatchNotificationRepliedAsync(Action<(uint id, string text)> handler, Action<Exception> onError = null);
        Task<IDisposable> WatchActivationTokenAsync(Action<(uint id, string activationToken)> handler, Action<Exception> onError = null);
        Task<T> GetAsync<T>(string prop);
        Task<NotificationsProperties> GetAllAsync();
        Task SetAsync(string prop, object val);
        Task<IDisposable> WatchPropertiesAsync(Action<PropertyChanges> handler);
    }

    [Dictionary]
    public class NotificationsProperties
    {
        private bool _inhibited = default(bool);
        public bool Inhibited
        {
            get
            {
                return _inhibited;
            }

            set
            {
                _inhibited = (value);
            }
        }
    }

    static class NotificationsExtensions
    {
        public static Task<bool> GetInhibitedAsync(this INotifications o) => o.GetAsync<bool>("Inhibited");
    }

    [DBusInterface("org.kde.NotificationManager")]
    interface INotificationManager : IDBusObject
    {
        Task RegisterWatcherAsync();
        Task UnRegisterWatcherAsync();
        Task InvokeActionAsync(uint Id, string ActionKey);
    }
}
