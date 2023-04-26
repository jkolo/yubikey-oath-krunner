using System.Runtime.CompilerServices;
using Tmds.DBus;

[assembly: InternalsVisibleTo(Tmds.DBus.Connection.DynamicAssemblyName)]
namespace KRunner.YOath.DBus.Secrets
{
    [DBusInterface("org.freedesktop.Secret.Session")]
    interface ISecretSession : IDBusObject
    {
        Task CloseAsync();
    }
}
