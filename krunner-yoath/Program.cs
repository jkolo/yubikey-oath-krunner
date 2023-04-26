using KRunner.YOath;
using KRunner.YOath.DBus;
using KRunner.YOath.DBus.Notifications;
using KRunner.YOath.DBus.Secrets;
using KRunner.YOath.Yubikey;
using KRunner.YOath.Yubikey.Oath;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Tmds.DBus;
using Yubico.Core.Logging;

var builder = Host.CreateApplicationBuilder(args);
builder.Services
    .AddSystemd()
    .AddHostedService<KrunnerYOathMainService>()
    .AddSingleton(new Connection(Address.Session))
    .AddSingleton<YubikeyDeviceCredentialsListener>()
    .AddSingleton<YOathKrunner>()
    .AddSingleton<OathCredentialsService>()
    .AddSingleton<IOathCredentialsService>(x => x.GetRequiredService<OathCredentialsService>())
    .AddSingleton<IKlipper>(provider =>
        provider.GetRequiredService<Connection>().CreateProxy<IKlipper>("org.kde.klipper", new ObjectPath("/klipper")))
    .AddSingleton<INotifications>(provider =>
        provider.GetRequiredService<Connection>().CreateProxy<INotifications>("org.freedesktop.Notifications",
            new ObjectPath("/org/freedesktop/Notifications")))
    .AddSingleton<ISecretCollection>(provider =>
        provider.GetRequiredService<Connection>().CreateProxy<ISecretCollection>("org.freedesktop.secrets",
            new ObjectPath("/org/freedesktop/secrets/aliases/default")))
    .AddSingleton<SecretService>()
    .AddSingleton<DHAesCbcPkcs7>();

var host = builder.Build();
Log.LoggerFactory = host.Services.GetRequiredService<ILoggerFactory>();

host.Run();
