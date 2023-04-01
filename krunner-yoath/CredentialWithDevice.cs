using Yubico.YubiKey;
using Yubico.YubiKey.Oath;

namespace KRunner.YOath;

public record CredentialWithDevice(Credential Credential, IYubiKeyDevice Device)
{
    public override string ToString() => $"{Device.SerialNumber}:{Credential.Name}";

    public static implicit operator string(CredentialWithDevice credentialWithDevice) => credentialWithDevice.ToString();
    public string DisplayText => Credential.Issuer ?? Credential.Name;

    public string? SubDisplayText => Credential.AccountName;

    public double Relevance(string query)
    {
        if (string.Equals(Credential.Issuer, query, StringComparison.InvariantCultureIgnoreCase))
            return 1.0d;
        if (Credential.Issuer?.Contains(query, StringComparison.InvariantCultureIgnoreCase) ?? false)
            return 0.8d;
        if (string.Equals(Credential.AccountName, query, StringComparison.InvariantCultureIgnoreCase))
            return 0.6d;
        if (Credential.AccountName?.Contains(query, StringComparison.InvariantCultureIgnoreCase) ?? false)
            return 0.4d;

        return 0.2d;
    }
}
