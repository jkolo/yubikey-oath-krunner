using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

static class ServiceExtensions
{
    public static Task<ObjectPath[]> GetCollectionsAsync(this ISecretService o) => o.GetAsync<ObjectPath[]>("Collections");
}
