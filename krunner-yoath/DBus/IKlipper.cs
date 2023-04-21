using Tmds.DBus;

namespace KRunner.YOath.DBus
{
    [DBusInterface("org.kde.klipper.klipper")]
    public interface IKlipper : IDBusObject
    {
        Task<string> getClipboardContentsAsync();
        Task setClipboardContentsAsync(string S);
        Task clearClipboardContentsAsync();
        Task clearClipboardHistoryAsync();
        Task saveClipboardHistoryAsync();
        Task<string[]> getClipboardHistoryMenuAsync();
        Task<string> getClipboardHistoryItemAsync(int I);
        Task showKlipperPopupMenuAsync();
        Task showKlipperManuallyInvokeActionMenuAsync();
    }
}
