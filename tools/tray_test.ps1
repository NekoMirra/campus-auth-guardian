Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# 找我们的托盘图标（通知区域溢出或可见区），并右键点击
$trayHwnd = (Get-Process CampusAuthGuardian -ErrorAction SilentlyContinue).MainWindowHandle
Write-Host "main hwnd: $trayHwnd"

# Win11 通知区：chevron 展开，然后枚举 ToolBar 按钮
# 简化路径：直接右键点击 chevron 展开的溢出面板中的图标位置
# 先点 chevron（任务栏最右侧）
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$chevronX = $screen.Width - 24
$chevronY = $screen.Height - 22

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class M {
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
}
"@

[M]::SetCursorPos($chevronX, $chevronY)
Start-Sleep -Milliseconds 300
[M]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero)  # RIGHTDOWN
[M]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero)  # RIGHTUP
Start-Sleep -Milliseconds 800
Write-Host "right-clicked chevron at $chevronX,$chevronY"
