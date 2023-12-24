#nullable disable
using System.Runtime.CompilerServices;
using Tmds.DBus;

namespace KRunner.YOath.DBus.Notifications
{
    [Dictionary]
    public record struct NotificationsProperties(
        [field: Property(Name = "Inhibited")]
        bool? Inhibited
    );
}