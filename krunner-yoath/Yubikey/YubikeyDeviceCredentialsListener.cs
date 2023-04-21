using KRunner.YOath.Yubikey.Oath;
using Yubico.YubiKey;

namespace KRunner.YOath.Yubikey;

public class YubikeyDeviceCredentialsListener
{
    private readonly OathCredentialsService _oathCredentialsService;

    public YubikeyDeviceCredentialsListener(OathCredentialsService oathCredentialsService)
    {
        _oathCredentialsService = oathCredentialsService;
    }
    
    public void Run()
    {
        YubiKeyDeviceListener.Instance.Arrived += (sender, eventArgs) => _oathCredentialsService.AddYubikey(eventArgs.Device);
        YubiKeyDeviceListener.Instance.Removed += (sender, eventArgs) => _oathCredentialsService.RemoveYubikey(eventArgs.Device);

        var yubiKeyDevices = YubiKeyDevice.FindAll().Where(yk => yk.HasFeature(YubiKeyFeature.OtpApplication));
        foreach (var yubiKeyDevice in yubiKeyDevices)
        {
            _oathCredentialsService.AddYubikey(yubiKeyDevice);
        }
    }
}
