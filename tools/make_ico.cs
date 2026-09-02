#r:System.Drawing.Common.dll
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

// 生成多尺寸 ICO（PNG 帧容器）
var sizes = new[] { 16, 32, 48, 256 };
var pngs = new List<byte[]>();
foreach (var s in sizes)
{
    using var bmp = new Bitmap(s, s);
    using var g = Graphics.FromImage(bmp);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    // 蓝色圆角背景
    using var brush = new LinearGradientBrush(
        new Rectangle(0, 0, s, s),
        Color.FromArgb(255, 0, 120, 212),
        Color.FromArgb(255, 0, 70, 150), 45f);
    using var path = new GraphicsPath();
    float r = Math.Max(2, s * 0.22f);
    path.AddArc(0, 0, r * 2, r * 2, 180, 90);
    path.AddArc(s - r * 2, 0, r * 2, r * 2, 270, 90);
    path.AddArc(s - r * 2, s - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(0, s - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
    g.FillPath(brush, path);
    // 白色 WiFi 弧线
    using var pen = new Pen(Color.White, Math.Max(1f, s * 0.08f));
    pen.StartCap = LineCap.Round;
    pen.EndCap = LineCap.Round;
    float cx = s / 2f, cy = s * 0.72f;
    foreach (var k in new[] { 0.14f, 0.28f, 0.42f })
        g.DrawArc(pen, cx - s * k, cy - s * k, s * k * 2, s * k * 2, 205, 130);
    float dot = Math.Max(1.5f, s * 0.06f);
    g.FillEllipse(Brushes.White, cx - dot, cy - dot, dot * 2, dot * 2);
    using var pngMs = new MemoryStream();
    bmp.Save(pngMs, ImageFormat.Png);
    pngs.Add(pngMs.ToArray());
}

var ms = new MemoryStream();
using (var bw = new BinaryWriter(ms, System.Text.Encoding.UTF8, true))
{
    bw.Write((ushort)0); bw.Write((ushort)1); bw.Write((ushort)pngs.Count);
    uint offset = 6 + (uint)(16 * pngs.Count);
    for (int i = 0; i < pngs.Count; i++)
    {
        byte size = (byte)(sizes[i] == 256 ? 0 : sizes[i]);
        bw.Write(size); bw.Write((byte)0);
        bw.Write((byte)0); bw.Write((byte)0);
        bw.Write((ushort)1); bw.Write((ushort)32);
        bw.Write((uint)pngs[i].Length); bw.Write(offset);
        offset += (uint)pngs[i].Length;
    }
    foreach (var p in pngs) bw.Write(p);
}
File.WriteAllBytes(args.Length > 0 ? args[0] : "app.ico", ms.ToArray());
Console.WriteLine($"ICO-OK {ms.Length} bytes");
