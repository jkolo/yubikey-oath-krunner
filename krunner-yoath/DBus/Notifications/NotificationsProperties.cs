#nullable disable
using System.Runtime.CompilerServices;
using Tmds.DBus;

[assembly: InternalsVisibleTo(Tmds.DBus.Connection.DynamicAssemblyName)]
namespace KRunner.YOath.DBus.Notifications
{
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
}
