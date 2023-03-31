using Tmds.DBus;

namespace KRunner.YOath.DbusKrunner;

[DBusInterface("org.kde.krunner1")]
public interface IKRunner : IDBusObject
{
    [return: Argument]
    Task<MatchResult[]> MatchAsync(string query);

    [return: Argument]
    Task<Action[]> ActionsAsync();

    Task RunAsync(string data, string actionId);
    Task TeardownAsync();

    [return: Argument]
    Task<Config> ConfigAsync();
}
