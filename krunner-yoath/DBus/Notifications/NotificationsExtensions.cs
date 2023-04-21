namespace KRunner.YOath.DBus.Notifications;

static class NotificationsExtensions
{
    public static Task<bool> GetInhibitedAsync(this INotifications o) => o.GetAsync<bool>("Inhibited");
}
