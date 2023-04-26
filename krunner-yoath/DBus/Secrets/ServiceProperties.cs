using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

[Dictionary]
class ServiceProperties
{
    public ObjectPath[] Collections { get; set; } = Array.Empty<ObjectPath>();
}
