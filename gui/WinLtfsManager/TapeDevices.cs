using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;

namespace WinLtfsManager;

internal static class TapeDevices
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern uint QueryDosDevice(string? name, char[] target, int capacity);

    // Namespace enumeration only: never opens or moves a tape device.
    public static string[] Enumerate()
    {
        for (int capacity = 4096; capacity <= 1048576; capacity *= 2)
        {
            var buffer = new char[capacity];
            uint count = QueryDosDevice(null, buffer, capacity);
            if (count != 0)
                return new string(buffer, 0, (int)count).Split('\0', StringSplitOptions.RemoveEmptyEntries)
                    .Where(n => Regex.IsMatch(n, "^Tape[0-9]+$", RegexOptions.IgnoreCase))
                    .OrderBy(n => n, StringComparer.OrdinalIgnoreCase).ToArray();
            int error = Marshal.GetLastWin32Error();
            if (error != 122) throw new Win32Exception(error);
        }
        throw new IOException("Device namespace exceeds enumeration limit.");
    }
}
