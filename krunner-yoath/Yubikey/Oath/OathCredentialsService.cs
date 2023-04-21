using System.Text;
using Yubico.YubiKey;
using Yubico.YubiKey.Oath;

namespace KRunner.YOath.Yubikey.Oath;

public interface IOathCredentialsService
{
    IEnumerable<CredentialWithDevice> GetCredentials(string query);
    CredentialWithDevice GetCredential(int deviceSerialNumber, string id);
    Task<Code> GetCode(CredentialWithDevice credentialWithDevice);
}

public class OathCredentialsService : IOathCredentialsService
{
    private readonly IDictionary<IYubiKeyDevice, IReadOnlyCollection<Credential>> _credentials =
        new Dictionary<IYubiKeyDevice, IReadOnlyCollection<Credential>>();

    private readonly SecretService _secretService;

    public OathCredentialsService(SecretService secretService)
    {
        _secretService = secretService;
    }

    public async Task AddYubikey(IYubiKeyDevice device) => _credentials.Add(device, await RetrieveCodes(device));

    public void RemoveYubikey(IYubiKeyDevice device) => _credentials.Remove(device);

    public IEnumerable<CredentialWithDevice> GetCredentials(string query) =>
        _credentials
            .SelectMany(x => x.Value.Select(y => new CredentialWithDevice(y, x.Key)))
            .Where(c => c.Credential.Name.Contains(query, StringComparison.InvariantCultureIgnoreCase));


    public CredentialWithDevice GetCredential(int deviceSerialNumber, string id)
    {
        var deviceWithCredentials = _credentials.Single(x => x.Key.SerialNumber == deviceSerialNumber);
        return new(deviceWithCredentials.Value.Single(x => x.Name == id), deviceWithCredentials.Key);
    }

    public async Task<Code> GetCode(CredentialWithDevice credentialWithDevice)
    {
        using var session = await OpenOathSession(credentialWithDevice.Device);
        return session.CalculateCredential(credentialWithDevice.Credential);
    }

    private async Task<IReadOnlyCollection<Credential>> RetrieveCodes(IYubiKeyDevice yubiKeyDevice)
    {
        using var oathSession = await OpenOathSession(yubiKeyDevice);
        try
        {
            return oathSession.CalculateAllCredentials().Keys.ToList();
        }
        catch
        {
            return Array.Empty<Credential>();
        }
    }

    private async Task<OathSession> OpenOathSession(IYubiKeyDevice yubiKeyDevice)
    {
        string? password = null;
        var oathSession = new OathSession(yubiKeyDevice)
        {
            KeyCollector = (data) =>
            {
                if (!string.IsNullOrEmpty(password))
                    data.SubmitValue(Encoding.UTF8.GetBytes(password));

                return true;
            }
        };

        if (oathSession.IsPasswordProtected)
        {
            do
            {
                if (!string.IsNullOrEmpty(password)) 
                    await _secretService.RemovePassword(yubiKeyDevice.SerialNumber);

                password = await _secretService.GetPassword(yubiKeyDevice.SerialNumber);
                if (string.IsNullOrEmpty(password))
                {
                    var passwordDialogService =
                        new PasswordDialogService(
                            $"Podaj hasło do yubikey {yubiKeyDevice.SerialNumber}",
                            "Hasło do yubikey"
                        );
                    password = passwordDialogService.GetPassword();
                    if (!string.IsNullOrEmpty(password))
                        await _secretService.StorePassword(yubiKeyDevice.SerialNumber, password);
                }
            } while (!oathSession.TryVerifyPassword());
        }

        
        return oathSession;
    }
}
