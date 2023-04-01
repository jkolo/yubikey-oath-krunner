using System.Collections.Immutable;
using System.Collections.ObjectModel;
using Yubico.YubiKey;
using Yubico.YubiKey.Oath;

namespace KRunner.YOath;

public class YubikeyOath
{
    private readonly IDictionary<IYubiKeyDevice, IReadOnlyCollection<Credential>> Credentials = new Dictionary<IYubiKeyDevice, IReadOnlyCollection<Credential>>();

    public void Run()
    {
        YubiKeyDeviceListener.Instance.Arrived += (sender, eventArgs) => Credentials.Add(eventArgs.Device, RetrieveCodes(eventArgs.Device));
        YubiKeyDeviceListener.Instance.Removed += (sender, eventArgs) => Credentials.Remove(eventArgs.Device);

        var yubiKeyDevices = YubiKeyDevice.FindAll().Where(yk => yk.HasFeature(YubiKeyFeature.OtpApplication));
        foreach (var yubiKeyDevice in yubiKeyDevices)
        {
            Credentials.Add(yubiKeyDevice, RetrieveCodes(yubiKeyDevice));
        }
    }

    private ImmutableArray<Credential> RetrieveCodes(IYubiKeyDevice yubiKeyDevice)
    {
        using var oathSession = OpenOathSession(yubiKeyDevice);
        try
        {
            return oathSession.CalculateAllCredentials().Keys.ToImmutableArray();
        }
        catch
        {
            return ImmutableArray<Credential>.Empty;
        }
    }

    private OathSession OpenOathSession(IYubiKeyDevice yubiKeyDevice1)
    {
        var oathSession = new OathSession(yubiKeyDevice1)
        {
            KeyCollector = KeyCollectorService
        };

        oathSession.TryVerifyPassword();

        return oathSession;
    }

    private bool KeyCollectorService(KeyEntryData data)
    {
        if (data.Request == KeyEntryRequest.VerifyOathPassword)
        {
            //TODO: Dodać obsługę haseł
            data.SubmitValue("dupa123"u8);
        }

        return true;
    }

    public IEnumerable<CredentialWithDevice> GetCredentials(string query) =>
        Credentials
            .SelectMany(x => x.Value.Select(y => new CredentialWithDevice(y, x.Key)))
            .Where(c => c.Credential.Name.Contains(query, StringComparison.InvariantCultureIgnoreCase));


    public CredentialWithDevice GetCredential(int deviceSerialNumber, string id)
    {
        var deviceWithCredentials = Credentials.Single(x => x.Key.SerialNumber == deviceSerialNumber);
        return new (deviceWithCredentials.Value.Single(x => x.Name == id), deviceWithCredentials.Key);
    }

    public Code GetCode(CredentialWithDevice credentialWithDevice)
    {
        using var session = OpenOathSession(credentialWithDevice.Device);
        return session.CalculateCredential(credentialWithDevice.Credential);
    }
}
