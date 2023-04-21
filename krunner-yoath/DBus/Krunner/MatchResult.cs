namespace KRunner.YOath.DBus.Krunner;

public record struct MatchResult(string Data, string DisplayText, string Icon, QueryMatch QueryMatch, double Relevance, MatchProperties Properties);
