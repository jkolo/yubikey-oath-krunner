using System.Runtime.CompilerServices;

[assembly: InternalsVisibleTo(Tmds.DBus.Connection.DynamicAssemblyName)]
namespace KRunner.YOath.DBus.Secrets
{
    static class ItemExtensions
    {
        public static Task<bool> GetLockedAsync(this IItem o) => o.GetAsync<bool>("Locked");
        public static Task<IDictionary<string, string>> GetAttributesAsync(this IItem o) => o.GetAsync<IDictionary<string, string>>("Attributes");
        public static Task<string> GetLabelAsync(this IItem o) => o.GetAsync<string>("Label");
        public static Task<string> GetTypeAsync(this IItem o) => o.GetAsync<string>("Type");
        public static Task<ulong> GetCreatedAsync(this IItem o) => o.GetAsync<ulong>("Created");
        public static Task<ulong> GetModifiedAsync(this IItem o) => o.GetAsync<ulong>("Modified");
        public static Task SetAttributesAsync(this IItem o, IDictionary<string, string> val) => o.SetAsync("Attributes", val);
        public static Task SetLabelAsync(this IItem o, string val) => o.SetAsync("Label", val);
        public static Task SetTypeAsync(this IItem o, string val) => o.SetAsync("Type", val);
    }
}
