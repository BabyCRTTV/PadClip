$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PadClipShell {
 [DllImport("user32", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32", EntryPoint="GetClassLongPtrW")] public static extern IntPtr ClassIcon(IntPtr window,int index);
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct Info {
  public IntPtr icon; public int index; public uint attributes;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=260)] public string display;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=80)] public string type;
 }
 [DllImport("shell32",EntryPoint="SHGetFileInfoW",CharSet=CharSet.Unicode)] public static extern IntPtr ShellIcon(string path,uint attrs,out Info info,uint size,uint flags);
}
'@
function CheckIcon($label,[IntPtr]$handle) {
 if ($handle -eq [IntPtr]::Zero) { throw "Missing $label icon" }
 $bitmap=[Drawing.Icon]::FromHandle($handle).ToBitmap()
 try {
  $red=0
  for($y=[int]($bitmap.Height*0.2);$y -lt $bitmap.Height*0.50;$y++){
   for($x=[int]($bitmap.Width*0.38);$x -lt $bitmap.Width*0.63;$x++){
    $p=$bitmap.GetPixel($x,$y)
    if($p.A -gt 160 -and $p.R -gt 170 -and $p.G -lt 130 -and $p.B -lt 130){$red++}
   }
  }
  if($red -lt 1){ throw "Missing recording dot: $label" }
  Write-Host "$label $($bitmap.Width)x$($bitmap.Height), $red red center pixels"
 } finally { $bitmap.Dispose() }
}
Expand-Archive ./8BitTrade-PadClip-Windows.zip ./dist
$exe=(Resolve-Path ./dist/PadClip.exe).Path
$app=Start-Process $exe -ArgumentList '--startup' -PassThru
$link=Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'PadClip.lnk'
try {
 for($n=0;$n -lt 80;$n++){ $w=[PadClipShell]::FindWindow('8BitTradePadClip',$null); if($w -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100 }
 CheckIcon 'Window large' ([PadClipShell]::ClassIcon($w,-14))
 CheckIcon 'Window small / tray artwork' ([PadClipShell]::ClassIcon($w,-34))
 $sh=New-Object -ComObject WScript.Shell
 $shortcut=$sh.CreateShortcut($link);$shortcut.TargetPath=$exe;$shortcut.Save()
 foreach($item in @($exe,$link)){
  $info=New-Object PadClipShell+Info
  $ok=[PadClipShell]::ShellIcon($item,0,[ref]$info,[Runtime.InteropServices.Marshal]::SizeOf($info),0x100)
  if ($ok -eq [IntPtr]::Zero){ throw "Shell could not extract icon" }
  CheckIcon ([IO.Path]::GetFileName($item)) $info.icon
 }
} finally { Remove-Item -LiteralPath $link -ErrorAction SilentlyContinue; if(!$app.HasExited){ $app.Kill() } }
