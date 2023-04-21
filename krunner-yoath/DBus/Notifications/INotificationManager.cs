using Tmds.DBus;

namespace KRunner.YOath.DBus.Notifications;

[DBusInterface("org.kde.NotificationManager")]
interface INotificationManager : IDBusObject
{
    Task RegisterWatcherAsync();
    Task UnRegisterWatcherAsync();
    Task InvokeActionAsync(uint Id, string ActionKey);
}