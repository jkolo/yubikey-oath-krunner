using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

[DBusInterface("org.freedesktop.Secret.Item")]
interface IItem : IDBusObject
{
    Task<ObjectPath> DeleteAsync();
    Task<(ObjectPath secret, byte[], byte[], string)> GetSecretAsync(ObjectPath Session);
    Task SetSecretAsync((ObjectPath, byte[], byte[], string) Secret);
    Task<T> GetAsync<T>(string prop);
    Task<ItemProperties> GetAllAsync();
    Task SetAsync(string prop, object val);
    Task<IDisposable> WatchPropertiesAsync(Action<PropertyChanges> handler);
}
