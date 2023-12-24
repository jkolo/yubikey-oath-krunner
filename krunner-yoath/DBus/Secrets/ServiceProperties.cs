using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

[Dictionary]
public record struct ServiceProperties(
    [field: Property(Name = "Collections")]
    ObjectPath[] Collections
    );
