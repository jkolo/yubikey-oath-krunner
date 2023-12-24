using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

[Dictionary]
public record struct ItemProperties(
    [field: Property(Name = "Locked")]
    bool Locked,
    [field: Property(Name = "Attributes")]
    IDictionary<string, string> Attributes,
    [field: Property(Name = "Label")]
    string Label,
    [field: Property(Name = "Type")]
    string Type,
    [field: Property(Name = "Created")]
    ulong Created,
    [field: Property(Name = "Modified")]
    ulong Modified
);