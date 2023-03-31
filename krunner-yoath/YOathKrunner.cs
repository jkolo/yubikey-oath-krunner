using System.Text.RegularExpressions;
using KRunner.YOath.DbusKrunner;
using Tmds.DBus;
using Action = KRunner.YOath.DbusKrunner.Action;

namespace KRunner.YOath;

public partial class YOathKrunner : IKRunner
{
    private readonly YubikeyOath _yubikeyOath;
    private readonly IKlipper _klipper;
    private readonly INotifications _notifications;

    public YOathKrunner(YubikeyOath yubikeyOath, IKlipper klipper, INotifications notifications)
    {
        _yubikeyOath = yubikeyOath;
        _klipper = klipper;
        _notifications = notifications;
    }

    public ObjectPath ObjectPath => new ObjectPath("/yoath");

    public Task<MatchResult[]> MatchAsync(string query)
    {
        var credentials = _yubikeyOath.GetCredentials(query);

        var matchResults = credentials.Select(x => new MatchResult(x, x.DisplayText, "yoathrunner", QueryMatch.ExactMatch, x.Relevance(query), new MatchProperties
        {
            subtext = x.SubDisplayText,
            actionids = Array.Empty<string>()
        })).ToArray();

        return Task.FromResult(matchResults);
    }

    public Task<Action[]> ActionsAsync() => Task.FromResult(Array.Empty<Action>());

    public async Task RunAsync(string data, string actionId)
    {
        var match = DeviceRegex().Match(data);
        var credentialName = match.Groups["credentialName"].Value;
        var deviceSerial = int.Parse(match.Groups["deviceSerial"].Value);

        var credentialWithDevice = _yubikeyOath.GetCredential(deviceSerial, credentialName);
        var touchNotifyId = default(uint?);
        
        if (credentialWithDevice.Credential.RequiresTouch ?? throw new InvalidOperationException("WTF"))
        {
            touchNotifyId = await _notifications.NotifyAsync(
                "YOATH",
                1,
                "yoathrunner",
                "Naciśnij przycisk YubiKey!",
                "",
                Array.Empty<string>(),
                new Dictionary<string, object>(),
                0);
        }

        var code = _yubikeyOath.GetCode(credentialWithDevice);
        if (touchNotifyId.HasValue)
            await _notifications.CloseNotificationAsync(touchNotifyId.Value);
        
        await _klipper.setClipboardContentsAsync(code.Value!);
        await _notifications.NotifyAsync(
            "YOATH",
            0,
            "yoathrunner",
            "Skopiowano kod do schowka",
            code.Value!,
            Array.Empty<string>(),
            new Dictionary<string, object>(),
            3000);
    }

    [GeneratedRegex(@"^(?<deviceSerial>\d+):(?<credentialName>.+)$")]
    private static partial Regex DeviceRegex();

    public Task TeardownAsync() => Task.CompletedTask;

    public Task<Config> ConfigAsync() => Task.FromResult(new Config(".*", 3, Array.Empty<string>(), Array.Empty<Action>()));
}
