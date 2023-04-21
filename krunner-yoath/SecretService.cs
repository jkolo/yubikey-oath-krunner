using System.Text;
using KRunner.YOath.DBus.Secrets;
using Tmds.DBus;

namespace KRunner.YOath;

public class SecretService
{
    private readonly Connection _connection;
    private readonly ISecretService _secretService;
    private readonly ISecretCollection _collection;

    public SecretService()
    {
        _connection = Connection.Session;
        _secretService = _connection.CreateProxy<ISecretService>("org.freedesktop.secrets",
            new ObjectPath("/org/freedesktop/secrets"));
        _collection = _connection.CreateProxy<ISecretCollection>("org.freedesktop.secrets",
            new ObjectPath("/org/freedesktop/secrets/aliases/default"));
    }

    private async Task<IItem?> GetItem(int? serialNumber)
    {
        // Open a new session
        var searchAttributes = new Dictionary<string, string>
            { { "yubikey-id", serialNumber?.ToString() ?? "unknown" } };

        // Search for items with the attribute 'example_key'
        var itemPaths = await _collection.SearchItemsAsync(searchAttributes);

        // Get the secrets of the found items
        return itemPaths.Select(itemPath => _connection.CreateProxy<IItem>("org.freedesktop.secrets", itemPath))
            .SingleOrDefault();
    }

    public async Task<string?> GetPassword(int? serialNumber)
    {
        var (_, sessionPath) = await _secretService.OpenSessionAsync("plain", Array.Empty<byte>());
        var session = _connection.CreateProxy<ISecretSession>("org.freedesktop.secrets", sessionPath);
        try
        {
            var item = await GetItem(serialNumber);
            if (item is not null)
            {
                var (_, _, secretBytes, _) = await item.GetSecretAsync(sessionPath);
                var result = Encoding.UTF8.GetString(secretBytes);
                return string.IsNullOrEmpty(result) ? null : result;
            }

            return null;
        }
        finally
        {
            await session.CloseAsync();
        }
    }

    public async Task StorePassword(int? serialNumber, string password)
    {
        var searchAttributes = new Dictionary<string, string>
            { { "yubikey-id", serialNumber?.ToString() ?? "unknown" } };
        var properties = new Dictionary<string, object>()
        {
            { "org.freedesktop.Secret.Item.Label", $"Yubikey {serialNumber}" },
            { "org.freedesktop.Secret.Item.Attributes", searchAttributes }
        };
        var (_, sessionPath) = await _secretService.OpenSessionAsync("plain", Array.Empty<byte>());
        var session = _connection.CreateProxy<ISecretSession>("org.freedesktop.secrets", sessionPath);
        try
        {
            await _collection.CreateItemAsync(properties,
                (sessionPath, Array.Empty<byte>(), Encoding.UTF8.GetBytes(password), "text/plain; charset=utf8"),
                true);
        }
        finally
        {
            await session.CloseAsync();
        }
    }

    public async Task RemovePassword(int? serialNumber)
    {
        var (_, sessionPath) = await _secretService.OpenSessionAsync("plain", Array.Empty<byte>());
        var session = _connection.CreateProxy<ISecretSession>("org.freedesktop.secrets", sessionPath);
        try
        {
            var item = await GetItem(serialNumber);
            if (item is not null) 
                await item.DeleteAsync();
        }
        finally
        {
            await session.CloseAsync();
        }
    }
}
