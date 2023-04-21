using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

[Dictionary]
class ServiceProperties
{
    private ObjectPath[] _collections = default(ObjectPath[]);
    public ObjectPath[] Collections
    {
        get
        {
            return _collections;
        }

        set
        {
            _collections = (value);
        }
    }
}