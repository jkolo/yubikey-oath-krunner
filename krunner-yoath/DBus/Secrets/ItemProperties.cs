using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

[Dictionary]
class ItemProperties
{
    public bool Locked { get; set; }
    public IDictionary<string, string> Attributes { get; set; } = new Dictionary<string, string>();
    public string Label { get; set; } = string.Empty;
    public string Type { get; set; } = string.Empty;
    public ulong Created { get; set; }
    public ulong Modified { get; set; }
}
