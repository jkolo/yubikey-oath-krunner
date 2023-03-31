using Tmds.DBus;

namespace KRunner.YOath.DbusKrunner;

[Dictionary]
public record struct Config(string MatchRegex, int MinLetterCount, string[] TriggerWords, Action[] Actions);
