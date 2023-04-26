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
    
    public async Task Run()
    {
        YubiKeyDeviceListener.Instance.Arrived += async (sender, eventArgs) => await _oathCredentialsService.AddYubikey(eventArgs.Device);
        YubiKeyDeviceListener.Instance.Removed += (sender, eventArgs) => _oathCredentialsService.RemoveYubikey(eventArgs.Device);

        var yubiKeyDevices = YubiKeyDevice.FindAll().Where(yk => yk.HasFeature(YubiKeyFeature.OtpApplication));
        foreach (var yubiKeyDevice in yubiKeyDevices)
        {
            await _oathCredentialsService.AddYubikey(yubiKeyDevice);
        }
    }
}
