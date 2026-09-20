using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;


namespace WinLtfsManager;

internal static class Program
{
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool AttachConsole(uint id);
    [DllImport("kernel32.dll")] static extern bool FreeConsole();
    [DllImport("kernel32.dll")] static extern bool SetConsoleCtrlHandler(nint handler, bool add);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool GenerateConsoleCtrlEvent(uint evt, uint group);

    [STAThread] static int Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "--signal")
        {
            FreeConsole();
            if (!AttachConsole(uint.Parse(args[1]))) return 1;
            SetConsoleCtrlHandler(0, true);
            bool ok = GenerateConsoleCtrlEvent(0, 0);
            Thread.Sleep(200);
            FreeConsole();
            return ok ? 0 : 2;
        }
        if (args.Length == 4 && args[0] == "--smoke")
        {
            try
            {
                using var s = new Session();
                s.Start(args[1], args[2], args[3], true);
                s.WaitReady().GetAwaiter().GetResult();
                s.Stop().GetAwaiter().GetResult();
                return 0;
            }
            catch { return 1; }
        }
        ApplicationConfiguration.Initialize();
        Application.Run(new Window());
        return 0;
    }
}

internal sealed class Session : IDisposable
{
    Process? process;
    Task? output, errors;
    readonly TaskCompletionSource ready = new(TaskCreationOptions.RunContinuationsAsynchronously);
    readonly object logLock = new();
    bool cleanUnmount;
    public string DirectoryPath { get; private set; } = "";
    public bool Exited => process == null || process.HasExited;
    public string Letter { get; private set; } = "";
    public void Start(string package, string device, string letter, bool simulated = false)
    {
        if (!System.Text.RegularExpressions.Regex.IsMatch(letter, "^[D-Z]:$"))
            throw new IOException("Select an available drive letter.");
        if (DriveInfo.GetDrives().Any(d => d.Name.StartsWith(letter, StringComparison.OrdinalIgnoreCase)))
            throw new IOException("Drive letter is already in use. Refresh the list.");
        if (!simulated && !TapeDevices.Enumerate().Contains(device, StringComparer.OrdinalIgnoreCase))
            throw new IOException("Tape drive已离线，请Refresh列表。");
        if (Process.GetProcessesByName("ltfs").Length != 0)
            throw new IOException("An LTFS process is already running. Unmount it in its original application first.");
        package = Path.GetFullPath(package);
        foreach (string file in new[] { "ltfs.exe", "libltfs.dll", "libdriver-ltotape-win.dll", "libiosched-unified.dll", "winfsp-x64.dll" })
            if (!File.Exists(Path.Combine(package, file))) throw new IOException("WinLtfs directory is missing " + file);
        DirectoryPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WinLtfsManager", "sessions", DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N")[..6]);
        Directory.CreateDirectory(DirectoryPath);
        string P(string file) => Path.Combine(package, file).Replace('\\', '/');
        string config = Path.Combine(DirectoryPath, "ltfs.conf");
        File.WriteAllText(config, $"plugin driver ltotape_win {P("libdriver-ltotape-win.dll")}\nplugin driver file {P("libdriver-file.dll")}\nplugin iosched unified {P("libiosched-unified.dll")}\ndefault driver ltotape_win\ndefault iosched unified\ndefault kmi none\n", new UTF8Encoding(false));
        Letter = letter;
        var info = new ProcessStartInfo(P("ltfs.exe")) { WorkingDirectory = package, UseShellExecute = false, CreateNoWindow = false, WindowStyle = ProcessWindowStyle.Hidden, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (var a in new[] { letter, "-f", "-o", "config_file=" + config.Replace('\\', '/'), "-o", "tape_backend=" + (simulated ? "file" : "ltotape_win"), "-o", "devname=" + device.Replace('\\', '/'), "-o", "work_directory=" + DirectoryPath.Replace('\\', '/'), "-o", "verbose=2" }) info.ArgumentList.Add(a);
        process = Process.Start(info) ?? throw new IOException("Could not start WinLtfs.");
        File.WriteAllText(Path.Combine(DirectoryPath, "pid.txt"), process.Id.ToString());
        output = Pump(process.StandardOutput);
        errors = Pump(process.StandardError);
    }
    async Task Pump(StreamReader reader)
    {
        while (await reader.ReadLineAsync() is { } line)
        {
            lock (logLock) File.AppendAllText(Path.Combine(DirectoryPath, "session.log"), line + Environment.NewLine);
            if (line.Contains("Volume unmounted successfully")) cleanUnmount = true;
            if (line.Contains("Ready to receive file system requests")) ready.TrySetResult();
        }
    }
    public async Task WaitReady()
    {
        var exit = process!.WaitForExitAsync();
        var winner = await Task.WhenAny(ready.Task, exit, Task.Delay(TimeSpan.FromMinutes(3)));
        if (winner != ready.Task) throw new IOException(Exited ? "Mount process exited. Check the logs." : "Mount is still pending. Use Unmount to request a clean stop; do not mount again.");
        for (int i = 0; i < 30 && !Exited; i++)
        {
            if (DriveInfo.GetDrives().Any(d => d.Name.StartsWith(Letter, StringComparison.OrdinalIgnoreCase))) return;
            await Task.Delay(200);
        }
        throw new IOException("Mount letter did not appear. Check the logs.");
    }
    public async Task Stop()
    {
        if (process == null) return;
        if (!process.HasExited)
        {
            var info = new ProcessStartInfo(Environment.ProcessPath!) { UseShellExecute = false, CreateNoWindow = true };
            info.ArgumentList.Add("--signal"); info.ArgumentList.Add(process.Id.ToString());
            using var signal = Process.Start(info)!;
            await signal.WaitForExitAsync();
            if (signal.ExitCode != 0 && !process.HasExited) throw new IOException("无法发送Unmount请求。保留程序运行，请Logs。");
            try { await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(90)); }
            catch (TimeoutException) { throw new IOException("Unmount is still pending. Process remains running; wait and retry. Do not power off."); }
        }
        await Task.WhenAll(output ?? Task.CompletedTask, errors ?? Task.CompletedTask);
        if (!cleanUnmount || process.ExitCode != 0) throw new IOException("Process exited without a confirmed clean unmount. Check logs for sync or device errors.");
    }
    public void Dispose() => process?.Dispose();
}

internal sealed class Window : Form
{
    string packagePath = "";
    readonly ComboBox device = new() { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill };
    readonly ComboBox letter = new() { DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill };
    readonly Button browse = new() { Text = "Locate WinLtfs", AutoSize = true };
    readonly Button refresh = new() { Text = "Refresh", AutoSize = true };
    readonly Button mount = new() { Text = "Mount", AutoSize = true };
    readonly Button unmount = new() { Text = "Unmount", AutoSize = true };
    readonly Button logs = new() { Text = "Logs", AutoSize = true };
    readonly Label status = new() { Text = "Not mounted", Dock = DockStyle.Fill, AutoSize = true };
    readonly System.Windows.Forms.Timer timer = new() { Interval = 1000 };
    Session? session;
    bool busy, closing;
    string lastLog = "";
    string Settings => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WinLtfsManager", "package.txt");
    public Window()
    {
        Text = "WinLtfs Manager"; ClientSize = new Size(820, 330); MinimumSize = new Size(820, 370);
        Font = new Font("Segoe UI", 10); StartPosition = FormStartPosition.CenterScreen;
        var grid = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(24), ColumnCount = 3, RowCount = 7 };
        grid.ColumnStyles.Add(new(SizeType.Absolute, 100)); grid.ColumnStyles.Add(new(SizeType.Percent, 100)); grid.ColumnStyles.Add(new(SizeType.AutoSize));
        grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 0));
        grid.Controls.Add(new Label { Text = "Tape drive", AutoSize = true }, 0, 1); grid.Controls.Add(device, 1, 1); grid.Controls.Add(refresh, 2, 1);
        grid.Controls.Add(new Label { Text = "Drive letter", AutoSize = true }, 0, 2); grid.Controls.Add(letter, 1, 2);
        var note = new Label { Text = "Mounting can update the tape index. Use cartridge write protection for read-only access.", AutoSize = true, ForeColor = Color.DimGray, Padding = new Padding(0, 12, 0, 12) };
        grid.Controls.Add(note, 0, 3); grid.SetColumnSpan(note, 3);
        var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true }; buttons.Controls.AddRange([mount, unmount, logs, browse]); grid.Controls.Add(buttons, 1, 4); grid.SetColumnSpan(buttons, 2);
        grid.Controls.Add(status, 0, 5); grid.SetColumnSpan(status, 3); Controls.Add(grid);
        if (File.Exists(Settings)) packagePath = File.ReadAllText(Settings).Trim();
        if (!File.Exists(Path.Combine(packagePath, "ltfs.exe")))
        {
            foreach (string candidate in new[] { Path.Combine(AppContext.BaseDirectory, "engine"), AppContext.BaseDirectory })
                if (File.Exists(Path.Combine(candidate, "ltfs.exe"))) { packagePath = candidate; break; }
            for (var dir = new DirectoryInfo(AppContext.BaseDirectory); !File.Exists(Path.Combine(packagePath, "ltfs.exe")) && dir != null; dir = dir.Parent)
            {
                string candidate = Path.Combine(dir.FullName, "dist");
                if (File.Exists(Path.Combine(candidate, "ltfs.exe"))) { packagePath = candidate; break; }
            }
        }
        browse.Click += (_, _) => { using var dialog = new FolderBrowserDialog { Description = "Select the WinLtfs directory containing ltfs.exe" }; if (dialog.ShowDialog(this) == DialogResult.OK) { packagePath = dialog.SelectedPath; Directory.CreateDirectory(Path.GetDirectoryName(Settings)!); File.WriteAllText(Settings, packagePath); status.Text = "Not mounted"; RefreshChoices(); } };
        refresh.Click += (_, _) => RefreshChoices();
        device.SelectedIndexChanged += (_, _) => UpdateButtons(); letter.SelectedIndexChanged += (_, _) => UpdateButtons();
        mount.Click += async (_, _) => await Mount(); unmount.Click += async (_, _) => await Stop();
        logs.Click += (_, _) => { if (Directory.Exists(lastLog)) Process.Start(new ProcessStartInfo(lastLog) { UseShellExecute = true }); };
        timer.Tick += (_, _) => { if (!busy && session?.Exited == true) { status.Text = "Mount process exited. Check logs before mounting again."; session.Dispose(); session = null; RefreshChoices(); } };
        FormClosing += async (_, e) => { if (closing || session == null) return; e.Cancel = true; if (busy) { status.Text = "操作进行中，请完成后Unmount再关闭。"; return; } await Stop(); if (session == null) { closing = true; Close(); } };
        FormClosed += (_, _) => timer.Dispose(); timer.Start(); RefreshChoices();
    }
    void RefreshChoices()
    {
        if (busy || session != null) return;
        try
        {
            string selected = device.Text, selectedLetter = letter.Text.Length == 0 ? "T:" : letter.Text;
            device.Items.Clear(); device.Items.AddRange(TapeDevices.Enumerate().Cast<object>().ToArray());
            if (device.Items.Contains(selected)) device.SelectedItem = selected; else if (device.Items.Count > 0) device.SelectedIndex = 0;
            var used = DriveInfo.GetDrives().Select(x => x.Name[..2]).ToHashSet(StringComparer.OrdinalIgnoreCase);
            letter.Items.Clear(); for (char c = 'D'; c <= 'Z'; c++) if (!used.Contains(c + ":")) letter.Items.Add(c + ":");
            if (letter.Items.Contains(selectedLetter)) letter.SelectedItem = selectedLetter; else if (letter.Items.Count > 0) letter.SelectedIndex = 0;
            if (device.Items.Count == 0) status.Text = "No tape drive detected. Power on the drive and refresh.";
        }
        catch (Exception ex) { status.Text = ex.Message; }
        UpdateButtons();
    }
    void UpdateButtons()
    {
        bool engineFound = File.Exists(Path.Combine(packagePath, "ltfs.exe"));
        browse.Visible = !engineFound;
        if (!engineFound && session == null) status.Text = "WinLtfs was not found. Locate the engine directory.";
        browse.Enabled = refresh.Enabled = device.Enabled = letter.Enabled = !busy && session == null;
        mount.Enabled = !busy && session == null && device.SelectedItem != null && letter.SelectedItem != null && File.Exists(Path.Combine(packagePath, "ltfs.exe"));
        unmount.Enabled = !busy && session != null; logs.Enabled = Directory.Exists(lastLog);
    }
    async Task Mount()
    {
        busy = true; session = new(); UpdateButtons(); status.Text = "Loading tape index, please wait...";
        try { session.Start(packagePath, device.Text, letter.Text); lastLog = session.DirectoryPath; await session.WaitReady(); status.Text = $"Mounted {letter.Text} · {device.Text}"; }
        catch (Exception ex) { status.Text = ex.Message; if (session?.Exited == true) { session.Dispose(); session = null; } }
        finally { busy = false; UpdateButtons(); }
    }
    async Task Stop()
    {
        if (session == null) return; busy = true; UpdateButtons(); status.Text = "Synchronizing and unmounting. Keep the tape drive powered on...";
        try { await session.Stop(); session.Dispose(); session = null; status.Text = "已Unmount，可以关闭Tape drive。"; }
        catch (Exception ex) { status.Text = ex.Message; if (session?.Exited == true) { session.Dispose(); session = null; } }
        finally { busy = false; RefreshChoices(); UpdateButtons(); }
    }
}
