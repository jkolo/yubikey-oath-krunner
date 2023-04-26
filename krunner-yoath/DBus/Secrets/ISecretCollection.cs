using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

[DBusInterface("org.freedesktop.Secret.Collection")]
interface ISecretCollection : IDBusObject
{
    Task<ObjectPath> DeleteAsync();
    Task<ObjectPath[]> SearchItemsAsync(IDictionary<string, string> Attributes);
    Task<(ObjectPath item, ObjectPath prompt)> CreateItemAsync(IDictionary<string, object> Properties, (ObjectPath, byte[], byte[], string) Secret, bool Replace);
    Task<IDisposable> WatchItemCreatedAsync(Action<ObjectPath> handler, Action<Exception>? onError = null);
    Task<IDisposable> WatchItemDeletedAsync(Action<ObjectPath> handler, Action<Exception>? onError = null);
    Task<IDisposable> WatchItemChangedAsync(Action<ObjectPath> handler, Action<Exception>? onError = null);
    Task<T> GetAsync<T>(string prop);
    Task<CollectionProperties> GetAllAsync();
    Task SetAsync(string prop, object val);
    Task<IDisposable> WatchPropertiesAsync(Action<PropertyChanges> handler);
}
