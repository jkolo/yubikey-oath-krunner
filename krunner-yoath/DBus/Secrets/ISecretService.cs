using Tmds.DBus;

namespace KRunner.YOath.DBus.Secrets;

[DBusInterface("org.freedesktop.Secret.Service")]
interface ISecretService : IDBusObject
{
    Task<(object output, ObjectPath result)> OpenSessionAsync(string Algorithm, object Input);
    Task<(ObjectPath collection, ObjectPath prompt)> CreateCollectionAsync(IDictionary<string, object> Properties, string Alias);
    Task<(ObjectPath[] unlocked, ObjectPath[] locked)> SearchItemsAsync(IDictionary<string, string> Attributes);
    Task<(ObjectPath[] unlocked, ObjectPath prompt)> UnlockAsync(ObjectPath[] Objects);
    Task<(ObjectPath[] locked, ObjectPath prompt)> LockAsync(ObjectPath[] Objects);
    Task<IDictionary<ObjectPath, (ObjectPath, byte[], byte[], string)>> GetSecretsAsync(ObjectPath[] Items, ObjectPath Session);
    Task<ObjectPath> ReadAliasAsync(string Name);
    Task SetAliasAsync(string Name, ObjectPath Collection);
    Task<IDisposable> WatchCollectionCreatedAsync(Action<ObjectPath> handler, Action<Exception>? onError = null);
    Task<IDisposable> WatchCollectionDeletedAsync(Action<ObjectPath> handler, Action<Exception>? onError = null);
    Task<IDisposable> WatchCollectionChangedAsync(Action<ObjectPath> handler, Action<Exception>? onError = null);
    Task<T> GetAsync<T>(string prop);
    Task<ServiceProperties> GetAllAsync();
    Task SetAsync(string prop, object val);
    Task<IDisposable> WatchPropertiesAsync(Action<PropertyChanges> handler);
}
