using Tmds.DBus;

namespace KRunner.YOath.DBus.Krunner;

[Dictionary]
public record struct Config(string MatchRegex, int MinLetterCount, string[] TriggerWords, Action[] Actions);
