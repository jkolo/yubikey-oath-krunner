using KRunner.YOath;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Tmds.DBus;

var builder = Host.CreateApplicationBuilder(args);
builder.Services
    .AddSystemd()
    .AddHostedService<YOATHService>()
    .AddSingleton(new Connection(Address.Session))
    .AddSingleton<YubikeyOath>()
    .AddSingleton<YOathKrunner>()
    .AddSingleton<IKlipper>(provider => provider.GetRequiredService<Connection>().CreateProxy<IKlipper>("org.kde.klipper", new ObjectPath("/klipper")))
    .AddSingleton<INotifications>(provider => provider.GetRequiredService<Connection>().CreateProxy<INotifications>("org.freedesktop.Notifications", new ObjectPath("/org/freedesktop/Notifications")));

var host = builder.Build();

host.Run();
