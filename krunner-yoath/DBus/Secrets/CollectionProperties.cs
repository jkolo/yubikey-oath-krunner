using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

[Dictionary]
class CollectionProperties
{
    private ObjectPath[] _items = default(ObjectPath[]);
    public ObjectPath[] Items
    {
        get
        {
            return _items;
        }

        set
        {
            _items = (value);
        }
    }

    private string _label = default(string);
    public string Label
    {
        get
        {
            return _label;
        }

        set
        {
            _label = (value);
        }
    }

    private bool _locked = default(bool);
    public bool Locked
    {
        get
        {
            return _locked;
        }

        set
        {
            _locked = (value);
        }
    }

    private ulong _created = default(ulong);
    public ulong Created
    {
        get
        {
            return _created;
        }

        set
        {
            _created = (value);
        }
    }

    private ulong _modified = default(ulong);
    public ulong Modified
    {
        get
        {
            return _modified;
        }

        set
        {
            _modified = (value);
        }
    }
}