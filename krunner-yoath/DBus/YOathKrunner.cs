using System.Diagnostics;
using System.Text.RegularExpressions;
using KRunner.YOath.DBus.Krunner;
using KRunner.YOath.DBus.Notifications;
using KRunner.YOath.Yubikey.Oath;
using Tmds.DBus;
using Yubico.YubiKey.Oath;
using Action = KRunner.YOath.DBus.Krunner.Action;

namespace KRunner.YOath.DBus;

public partial class YOathKrunner : IKRunner
{
    private readonly IOathCredentialsService _oathCredentialsService;
    private readonly IKlipper _klipper;
    private readonly INotifications _notifications;

    public YOathKrunner(IOathCredentialsService oathCredentialsService, IKlipper klipper, INotifications notifications)
    {
        _oathCredentialsService = oathCredentialsService;
        _klipper = klipper;
        _notifications = notifications;
    }

    public ObjectPath ObjectPath => new ObjectPath("/yoath");

    public Task<MatchResult[]> MatchAsync(string query)
    {
        var credentials = _oathCredentialsService.GetCredentials(query);

        var matchResults = credentials.Select(x => new MatchResult(x, x.DisplayText, "krunner_yoath", QueryMatch.ExactMatch, x.Relevance(query), new MatchProperties
        {
            Subtext = x.SubDisplayText,
            ActionIds = ["TypeId", "CopyId"]
        })).ToArray();

        return Task.FromResult(matchResults);
    }

    public Task<Action[]> ActionsAsync() => Task.FromResult(new []
    {
        new Action("TypeId", "Type to current window", "keyboard"),
        new Action("CopyId", "Copy to Clipboard", "edit-copy"),
    });

    public async Task RunAsync(string data, string actionId)
    {
        var match = DeviceRegex().Match(data);
        var credentialName = match.Groups["credentialName"].Value;
        var deviceSerial = int.Parse(match.Groups["deviceSerial"].Value);

        var credentialWithDevice = _oathCredentialsService.GetCredential(deviceSerial, credentialName);
        var touchNotifyId = default(uint?);
        Code code;
        
        try
        {
            if (credentialWithDevice.Credential.RequiresTouch ?? throw new InvalidOperationException("WTF"))
            {
                touchNotifyId = await NotifyAsync("Naciśnij przycisk YubiKey!", "", 0);
            }
            
            code = await _oathCredentialsService.GetCode(credentialWithDevice);
        }
        finally
        {
            if (touchNotifyId.HasValue)
                await _notifications.CloseNotificationAsync(touchNotifyId.Value);    
        }

        switch (actionId)
        {
            case "TypeId":
                if (credentialWithDevice.Credential.RequiresTouch != true)
                    await Task.Delay(TimeSpan.FromSeconds(1));

                var process = new Process()
                {
                    StartInfo = new ProcessStartInfo
                    {
                        FileName = "/usr/bin/ydotool",
                        Arguments = "type -f -",
                        RedirectStandardInput = true,
                        UseShellExecute = false,
                        CreateNoWindow = true,
                    }
                };
                
                process.Start();
                await process.StandardInput.WriteAsync(code.Value);
                await process.StandardInput.FlushAsync();
                process.StandardInput.Close();
                await process.WaitForExitAsync();
                break;
            
            default:
                await _klipper.setClipboardContentsAsync(code.Value!);

                var until = TimeSpan.FromSeconds(15);

                await NotifyAsync("Skopiowano kod do schowka", code.Value!, (int)until.TotalMilliseconds);

                await Task.Delay(until);

                await _klipper.clearClipboardContentsAsync();       
                break;
        }
    }

    private async Task<uint> NotifyAsync(string summary, string body, int timeout) =>
        await _notifications.NotifyAsync(
            "YOATH",
            0,
            "krunner_yoath",
            summary,
            body,
            Array.Empty<string>(),
            new Dictionary<string, object>(),
            timeout);

    [GeneratedRegex(@"^(?<deviceSerial>\d+):(?<credentialName>.+)$")]
    private static partial Regex DeviceRegex();

    public Task TeardownAsync() => Task.CompletedTask;

    public Task<Config> ConfigAsync() => Task.FromResult(new Config(".*", 3, Array.Empty<string>(), Array.Empty<Action>()));
}
