using KRunner.YOath.Yubikey;
using Microsoft.Extensions.Hosting;
using Tmds.DBus;

namespace KRunner.YOath;

public sealed class KrunnerYOathMainService : IHostedService, IDisposable
{
    private const string ServiceName = "pl.kolosowscy.yoath";
    
    private readonly Connection _connection;
    private readonly DBus.YOathKrunner _yOathKrunner;
    private readonly YubikeyDeviceCredentialsListener _yubikeyDeviceCredentialsListener;

    public KrunnerYOathMainService(Connection connection, DBus.YOathKrunner yOathKrunner, YubikeyDeviceCredentialsListener yubikeyDeviceCredentialsListener)
    {
        _connection = connection;
        _yOathKrunner = yOathKrunner;
        _yubikeyDeviceCredentialsListener = yubikeyDeviceCredentialsListener;
    }

    ~KrunnerYOathMainService()
    {
        Dispose(false);
    }

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        await _connection.ConnectAsync();
        await _connection.RegisterObjectAsync(_yOathKrunner);
        await _connection.RegisterServiceAsync(ServiceName, ServiceRegistrationOptions.Default);

        _yubikeyDeviceCredentialsListener.Run();
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        if (disposing)
        {
            _connection.Dispose();
        }
    }
}
