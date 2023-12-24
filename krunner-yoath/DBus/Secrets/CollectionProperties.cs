using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

[Dictionary]
public record struct CollectionProperties(
    [field: Property(Name = "Items")]
    ObjectPath[]? Items,
    [field: Property(Name = "Label")]
    string? Label,
    [field: Property(Name = "Locked")]
    bool? Locked,
    [field: Property(Name = "Created")]
    ulong? Created,
    [field: Property(Name = "Modified")]
    ulong? Modified);
