using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

internal sealed class TwoViews : Form
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Rect
    {
        public int Left, Top, Right, Bottom;
        public Rect(Rectangle area)
        {
            Left = area.Left; Top = area.Top; Right = area.Right; Bottom = area.Bottom;
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Properties
    {
        public uint Flags;
        public Rect Destination, Source;
        public byte Opacity;
        [MarshalAs(UnmanagedType.Bool)] public bool Visible, ClientOnly;
    }

    [DllImport("dwmapi.dll")] private static extern int DwmRegisterThumbnail(IntPtr destination, IntPtr source, out IntPtr id);
    [DllImport("dwmapi.dll")] private static extern int DwmUpdateThumbnailProperties(IntPtr id, ref Properties properties);
    [DllImport("dwmapi.dll")] private static extern int DwmUnregisterThumbnail(IntPtr id);
    [DllImport("dwmapi.dll")] private static extern int DwmFlush();
    [DllImport("dwmapi.dll")] private static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out int value, int bytes);
    [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] private static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out int pid);
    [DllImport("user32.dll")] private static extern bool SetProcessDPIAware();

    private readonly IntPtr[] windows, thumbnails = new IntPtr[2];
    private readonly Rectangle[] panes = new Rectangle[2];
    private readonly int[] processIds = new int[2];
    private readonly string output, dolphinPath;
    private readonly Timer timer = new Timer();
    private readonly Font labelFont = new Font("Segoe UI", 12f, FontStyle.Bold);
    private bool fullscreen, ready;
    private Rectangle normal;

    private TwoViews(IntPtr left, IntPtr right, string directory)
    {
        windows = new IntPtr[] { left, right };
        output = directory;
        if (left == right || !IsWindow(left) || !IsWindow(right))
            throw new ArgumentException("Two distinct live renderer window handles are required.");
        for (int i = 0; i < 2; ++i) GetWindowThreadProcessId(windows[i], out processIds[i]);
        using (var dolphin = Process.GetProcessById(processIds[0])) dolphinPath = dolphin.MainModule.FileName;
        Text = "fCavEX | Two Wii views";
        BackColor = Color.FromArgb(20, 33, 39);
        StartPosition = FormStartPosition.CenterScreen;
        var available = Screen.PrimaryScreen.WorkingArea;
        int width = Math.Min(1176, available.Width - 48);
        ClientSize = new Size(width, Math.Min(available.Height - 72, width * 3 / 8 + 58));
        MinimumSize = new Size(460, 260);
        DoubleBuffered = true;
        KeyPreview = true;
        Shown += delegate {
            for (int i = 0; i < windows.Length; ++i)
                Marshal.ThrowExceptionForHR(DwmRegisterThumbnail(Handle, windows[i], out thumbnails[i]));
            ready = true;
            LayoutViews();
            File.WriteAllText(Path.Combine(output, "compositor.txt"),
                "Live Windows DWM client-area thumbnails" + Environment.NewLine +
                "Host HWND " + left + Environment.NewLine + "Peer HWND " + right + Environment.NewLine +
                "Projector HWND " + Handle + Environment.NewLine);
            timer.Interval = 250;
            timer.Tick += OnTick;
            timer.Start();
        };
        Resize += delegate { if (ready) LayoutViews(); };
        KeyDown += delegate(object sender, KeyEventArgs e) {
            if (e.KeyCode == Keys.F11) {
                if (!fullscreen) {
                    normal = Bounds;
                    FormBorderStyle = FormBorderStyle.None;
                    Bounds = Screen.FromControl(this).Bounds;
                } else {
                    FormBorderStyle = FormBorderStyle.Sizable;
                    Bounds = normal;
                }
                fullscreen = !fullscreen;
                e.Handled = true;
            } else if (e.KeyCode == Keys.F5 && windows[1] == IntPtr.Zero) {
                File.WriteAllText(Path.Combine(output, "rejoin.request"), "");
            } else if (e.KeyCode == Keys.Escape) Close();
        };
    }

    private static Rectangle Fit(Rectangle box, int sourceWidth, int sourceHeight)
    {
        double ratio = Math.Min((double)box.Width / sourceWidth, (double)box.Height / sourceHeight);
        int w = Math.Max(1, (int)(sourceWidth * ratio)), h = Math.Max(1, (int)(sourceHeight * ratio));
        return new Rectangle(box.X + (box.Width-w)/2, box.Y + (box.Height-h)/2, w, h);
    }

    private void LayoutViews()
    {
        int width = ClientSize.Width / 2;
        for (int i = 0; i < windows.Length; ++i) {
            Rect source;
            if (windows[i] == IntPtr.Zero) continue;
            if (!GetClientRect(windows[i], out source)) continue;
            var frame = new Rectangle(i * width + 5, 39, width - 10, Math.Max(1, ClientSize.Height - 47));
            panes[i] = Fit(frame, Math.Max(1,source.Right-source.Left), Math.Max(1,source.Bottom-source.Top));
            var properties = new Properties {
                Flags = 1 | 4 | 8 | 16, Destination = new Rect(panes[i]), Opacity = 255,
                Visible = true, ClientOnly = true
            };
            Marshal.ThrowExceptionForHR(DwmUpdateThumbnailProperties(thumbnails[i], ref properties));
        }
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        string[] labels = { "Player A   /   host", "Player B   /   peer" };
        for (int i = 0; i < 2; i++) e.Graphics.DrawString(labels[i], labelFont, Brushes.White,
            new Point(i * ClientSize.Width / 2 + 12, 8));
        if (windows[1] == IntPtr.Zero)
            e.Graphics.DrawString("Player B disconnected", labelFont, Brushes.Gainsboro,
                new Point(ClientSize.Width / 2 + 16, ClientSize.Height / 2));
    }

    private bool IsUncovered(Rectangle bounds)
    {
        for (IntPtr top = GetWindow(Handle, 3); top != IntPtr.Zero; top = GetWindow(top, 3)) {
            Rect r;
            int cloaked;
            if (IsWindowVisible(top) && (DwmGetWindowAttribute(top, 14, out cloaked, 4) != 0 || cloaked == 0) && GetWindowRect(top, out r) &&
                bounds.IntersectsWith(Rectangle.FromLTRB(r.Left, r.Top, r.Right, r.Bottom))) return false;
        }
        return true;
    }

    private static uint Sample(Bitmap image, out int colors)
    {
        var distinct = new HashSet<int>();
        uint hash = 2166136261;
        for (int y=0; y<image.Height; y+=8)
            for (int x=0; x<image.Width; x+=8) {
                int rgb = image.GetPixel(x, y).ToArgb();
                distinct.Add(rgb);
                hash = unchecked((hash ^ (uint)rgb) * 16777619);
            }
        colors = distinct.Count;
        return hash;
    }

    private static bool OwnedWindow(IntPtr window, int expectedPid)
    {
        int pid;
        return window != IntPtr.Zero && IsWindow(window) &&
            GetWindowThreadProcessId(window, out pid) != 0 && pid == expectedPid;
    }

    private void LogConnection(string state)
    {
        File.AppendAllText(Path.Combine(output, "projector-connections.txt"),
            DateTime.UtcNow.ToString("o") + " " + state + " hostWindow=" + windows[0] +
            " peerWindow=" + windows[1] + " projector=" + Handle + Environment.NewLine);
    }

    private void UpdatePeer()
    {
        if (windows[1] != IntPtr.Zero && !OwnedWindow(windows[1], processIds[1])) {
            DwmUnregisterThumbnail(thumbnails[1]);
            thumbnails[1] = windows[1] = IntPtr.Zero;
            LogConnection("disconnected");
            Invalidate();
        }
        if (windows[1] != IntPtr.Zero) return;
        string file = Path.Combine(output, "peer-source.txt");
        if (!File.Exists(file)) return;
        string[] parts;
        try { parts = File.ReadAllText(file).Trim().Split(' '); } catch (IOException) { return; }
        int pid; long value;
        if (parts.Length != 2 || !int.TryParse(parts[0], out pid) || !long.TryParse(parts[1], out value)) return;
        var target = new IntPtr(value);
        if (pid == processIds[0] || !OwnedWindow(target, pid)) return;
        try {
            using (var candidate = Process.GetProcessById(pid)) {
                if (!String.Equals(dolphinPath, candidate.MainModule.FileName, StringComparison.OrdinalIgnoreCase) ||
                    candidate.MainWindowHandle != target) return;
            }
        } catch (ArgumentException) { return; } catch (InvalidOperationException) { return; }
          catch (System.ComponentModel.Win32Exception) { return; }
        if (DwmRegisterThumbnail(Handle, target, out thumbnails[1]) < 0) return;
        windows[1] = target; processIds[1] = pid;
        LayoutViews();
        LogConnection("rejoined");
        File.Delete(file);
    }

    private void OnTick(object sender, EventArgs args)
    {
        if (!OwnedWindow(windows[0], processIds[0])) { Close(); return; }
        UpdatePeer();
        string request = Path.Combine(output, "capture.request");
        if (!File.Exists(request)) return;
        string name;
        try { name = File.ReadAllText(request).Trim(); } catch (IOException) { return; }
        if (name != "in-game" && name != "after-edit" && name != "startup" && name != "avatar-view" &&
            name != "peer-offline" && name != "peer-rejoined") return;
        // Display only the requested projection over the desktop during its scoped snapshot.
        TopMost = true;
        BringToFront();
        DwmFlush();
        try {
            Rectangle bounds = RectangleToScreen(ClientRectangle);
            // Capture only unobscured pixels owned by our projector, including its child client area.
            // Do not save the user's desktop, even if a different app covers part of this window.
            if (!IsUncovered(bounds)) return;
            using (var bitmap = new Bitmap(bounds.Width, bounds.Height, PixelFormat.Format24bppRgb)) {
                using (var drawing = Graphics.FromImage(bitmap))
                    drawing.CopyFromScreen(bounds.Location, Point.Empty, bounds.Size);
                if (!IsUncovered(bounds) || bounds != RectangleToScreen(ClientRectangle)) return;
                bitmap.Save(Path.Combine(output, name + ".png"), ImageFormat.Png);
                int[] colors = new int[2];
                uint[] hashes = new uint[2];
                for (int i = 0; i < 2; i++)
                    using (Bitmap pane = bitmap.Clone(panes[i], PixelFormat.Format24bppRgb)) {
                        hashes[i] = Sample(pane, out colors[i]);
                        pane.Save(Path.Combine(output, name + (i == 0 ? "-A.png" : "-B.png")), ImageFormat.Png);
                    }
                bool distinctViews = colors[0] >= 12 && colors[1] >= 12 && hashes[0] != hashes[1];
                File.WriteAllText(Path.Combine(output, name + "-frames.txt"),
                    "distinctDetailedFrames=" + distinctViews + Environment.NewLine +
                    "A sampledColors=" + colors[0] + " hash=" + hashes[0].ToString("X8") + Environment.NewLine +
                    "B sampledColors=" + colors[1] + " hash=" + hashes[1].ToString("X8") + Environment.NewLine);
            }
            File.Delete(request);
        } finally { TopMost = false; }
    }

    protected override void OnFormClosed(FormClosedEventArgs args)
    {
        timer.Dispose();
        foreach (IntPtr thumb in thumbnails) if (thumb != IntPtr.Zero) DwmUnregisterThumbnail(thumb);
        labelFont.Dispose();
        base.OnFormClosed(args);
    }

    [STAThread] private static int Main(string[] args)
    {
        if (args.Length != 3) return 2;
        try {
            SetProcessDPIAware();
            Application.EnableVisualStyles();
            Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException);
            Application.Run(new TwoViews(new IntPtr(long.Parse(args[0])),
                new IntPtr(long.Parse(args[1])), Path.GetFullPath(args[2])));
            return 0;
        } catch (Exception error) {
            File.WriteAllText(Path.Combine(args[2], "compositor-error.txt"), error.ToString());
            return 1;
        }
    }
}
