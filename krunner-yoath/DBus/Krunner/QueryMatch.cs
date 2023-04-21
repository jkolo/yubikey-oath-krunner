namespace KRunner.YOath.DBus.Krunner;

public enum QueryMatch
{
    NoMatch = 0,
    CompletionMatch = 10,
    PossibleMatch = 30,
    InformationalMatch = 50,
    HelperMatch = 70,
    ExactMatch = 100
}
