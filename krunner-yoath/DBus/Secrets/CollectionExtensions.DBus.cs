using System.Runtime.CompilerServices;
using Tmds.DBus;

[assembly: InternalsVisibleTo(Tmds.DBus.Connection.DynamicAssemblyName)]
namespace KRunner.YOath.DBus.Secrets
{
    static class CollectionExtensions
    {
        public static Task<ObjectPath[]> GetItemsAsync(this ISecretCollection o) => o.GetAsync<ObjectPath[]>("Items");
        public static Task<string> GetLabelAsync(this ISecretCollection o) => o.GetAsync<string>("Label");
        public static Task<bool> GetLockedAsync(this ISecretCollection o) => o.GetAsync<bool>("Locked");
        public static Task<ulong> GetCreatedAsync(this ISecretCollection o) => o.GetAsync<ulong>("Created");
        public static Task<ulong> GetModifiedAsync(this ISecretCollection o) => o.GetAsync<ulong>("Modified");
        public static Task SetLabelAsync(this ISecretCollection o, string val) => o.SetAsync("Label", val);
    }
}
