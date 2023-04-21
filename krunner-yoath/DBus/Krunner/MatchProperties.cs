using Tmds.DBus;

namespace KRunner.YOath.DBus.Krunner;

[Dictionary]
public record MatchProperties
{
    public string[]? urls;
    public string? category;
    public string? subtext;
    public string[]? actionids;
}
