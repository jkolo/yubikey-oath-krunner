using System.Diagnostics;

namespace KRunner.YOath;

public interface IPasswordDialogService
{
    string? GetPassword();
}

public class PasswordDialogService : IPasswordDialogService
{
    private readonly string _description;
    private readonly string _title;
    private readonly string _prompt;

    public PasswordDialogService(string description, string title, string prompt = "Enter password:")
    {
        _description = description;
        _title = title;
        _prompt = prompt;
    }

    public string? GetPassword()
    {
        // Launch Pinentry to obtain a passphrase
        var startInfo = new ProcessStartInfo
        {
            FileName = "pinentry",
            RedirectStandardInput = true,
            RedirectStandardOutput = true
        };

        using var process = new Process { StartInfo = startInfo };
        process.Start();

// Send commands to Pinentry's standard input
        using var writer = process.StandardInput;
        writer.WriteLine($"SETDESC {_description}");
        writer.WriteLine($"SETTITLE {_title}");
        writer.WriteLine($"SETPROMPT {_prompt}");
        writer.WriteLine("GETPIN");
        writer.WriteLine("BYE"); // Send BYE command to Pinentry
        writer.Flush();

// Wait for process to complete
        process.WaitForExit();

// Read passphrase from Pinentry's output
        var password = ReadPassword(process.StandardOutput);

        return password;
    }

    private static string? ReadPassword(TextReader streamOutput)
    {
        while (streamOutput.ReadLine() is { } output)
        {
            if (output.StartsWith("D "))
            {
                // Password prefix, remove it
                return output.Substring(2).TrimEnd('\n');
            }

            if (output.StartsWith("ERR"))
            {
                var tokens = output.Split(new[] { ' ' }, 3);
                var errorCode = int.Parse(tokens[1]);

                if (errorCode == 83886179)
                {
                    // User clicked the "Cancel" button
                    return null;
                }

                throw new Exception("Pinentry error: " + output.Substring(4));
            }
        }

        return null;
    }
}
