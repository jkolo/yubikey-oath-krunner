using Tmds.DBus;

namespace KRunner.YOath.DBus.Krunner;

[Dictionary]
public record struct Config(
    [field: Property(Name = "MatchRegex")]
    string MatchRegex, 
    [field: Property(Name = "MinLetterCount")]
    int MinLetterCount, 
    [field: Property(Name = "TriggerWords")]
    string[] TriggerWords, 
    [field: Property(Name = "Actions")]
    Action[] Actions);
