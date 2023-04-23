using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

[Dictionary]
class CollectionProperties
{
    public ObjectPath[] Items { get; set; } = Array.Empty<ObjectPath>();
    public string Label { get; set; } = string.Empty;
    public bool Locked { get; set; }
    public ulong Created { get; set; }
    public ulong Modified { get; set; }
}
