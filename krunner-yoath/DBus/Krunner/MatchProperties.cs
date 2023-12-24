using Tmds.DBus;

namespace KRunner.YOath.DBus.Krunner;

[Dictionary]
public record struct MatchProperties(
    [field: Property(Name = "Urls")]
    string[]? Urls, 
    [field: Property(Name = "Category")]
    string? Category, 
    [field: Property(Name = "Subtext")]
    string? Subtext, 
    [field: Property(Name = "ActionIds")]
    string[]? ActionIds);
