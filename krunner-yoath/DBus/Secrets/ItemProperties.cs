using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

[Dictionary]
class ItemProperties
{
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

    private IDictionary<string, string> _attributes = default(IDictionary<string, string>);
    public IDictionary<string, string> Attributes
    {
        get
        {
            return _attributes;
        }

        set
        {
            _attributes = (value);
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

    private string _type = default(string);
    public string Type
    {
        get
        {
            return _type;
        }

        set
        {
            _type = (value);
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
