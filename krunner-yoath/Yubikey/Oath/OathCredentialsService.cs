using Yubico.YubiKey;
using Yubico.YubiKey.Oath;

namespace KRunner.YOath.Yubikey.Oath;

public interface IOathCredentialsService
{
    IEnumerable<CredentialWithDevice> GetCredentials(string query);
    CredentialWithDevice GetCredential(int deviceSerialNumber, string id);
    Code GetCode(CredentialWithDevice credentialWithDevice);
}

public class OathCredentialsService : IOathCredentialsService
{
    private readonly IDictionary<IYubiKeyDevice, IReadOnlyCollection<Credential>> _credentials = new Dictionary<IYubiKeyDevice, IReadOnlyCollection<Credential>>();

    public void AddYubikey(IYubiKeyDevice device) => _credentials.Add(device, RetrieveCodes(device));

    public void RemoveYubikey(IYubiKeyDevice device) => _credentials.Remove(device);
    
    public IEnumerable<CredentialWithDevice> GetCredentials(string query) =>
        _credentials
            .SelectMany(x => x.Value.Select(y => new CredentialWithDevice(y, x.Key)))
            .Where(c => c.Credential.Name.Contains(query, StringComparison.InvariantCultureIgnoreCase));


    public CredentialWithDevice GetCredential(int deviceSerialNumber, string id)
    {
        var deviceWithCredentials = _credentials.Single(x => x.Key.SerialNumber == deviceSerialNumber);
        return new (deviceWithCredentials.Value.Single(x => x.Name == id), deviceWithCredentials.Key);
    }

    public Code GetCode(CredentialWithDevice credentialWithDevice)
    {
        using var session = OpenOathSession(credentialWithDevice.Device);
        return session.CalculateCredential(credentialWithDevice.Credential);
    }

    private IReadOnlyCollection<Credential> RetrieveCodes(IYubiKeyDevice yubiKeyDevice)
    {
        using var oathSession = OpenOathSession(yubiKeyDevice);
        try
        {
            return oathSession.CalculateAllCredentials().Keys.ToList();
        }
        catch
        {
            return Array.Empty<Credential>();
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
}
