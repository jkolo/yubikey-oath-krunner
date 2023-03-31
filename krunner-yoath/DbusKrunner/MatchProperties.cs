using Tmds.DBus;

namespace KRunner.YOath.DbusKrunner;

[Dictionary]
public record MatchProperties
{
    public string[]? urls;
    public string? category;
    public string? subtext;
    public string[]? actionids;
}
