using Microsoft.Extensions.Hosting;
using Tmds.DBus;

namespace KRunner.YOath;

public class YOATHService : IHostedService, IDisposable
{
    private readonly Connection _connection;
    private readonly YOathKrunner _yOathKrunner;
    private readonly YubikeyOath _yubikeyOath;

    public YOATHService(Connection connection, YOathKrunner yOathKrunner, YubikeyOath yubikeyOath)
    {
        _connection = connection;
        _yOathKrunner = yOathKrunner;
        _yubikeyOath = yubikeyOath;
    }

    ~YOATHService()
    {
        Dispose(false);
    }

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        await _connection.ConnectAsync();
        await _connection.RegisterObjectAsync(_yOathKrunner);
        await _connection.RegisterServiceAsync("pl.kolosowscy.yoath", ServiceRegistrationOptions.Default);

        _yubikeyOath.Run();
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (disposing)
        {
            _connection.Dispose();
        }
    }
}
