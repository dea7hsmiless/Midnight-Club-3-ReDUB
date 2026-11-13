param()
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ReDUBVibrationTest {
    [StructLayout(LayoutKind.Sequential)] public struct Vibration { public ushort Left; public ushort Right; }
    [DllImport("xinput9_1_0.dll")] public static extern uint XInputSetState(uint index, ref Vibration vibration);
}
'@
$pad=$null
$v=New-Object ReDUBVibrationTest+Vibration
for($i=0;$i -lt 4;$i++){if([ReDUBVibrationTest]::XInputSetState($i,[ref]$v) -eq 0){$pad=$i;break}}
if($null -eq $pad){throw 'No Xbox/XInput controller detected. Connect it and try again.'}
try {
    Write-Host "Testing controller $($pad+1): left motor, then right motor."
    $v.Left=32767;$v.Right=0
    if([ReDUBVibrationTest]::XInputSetState($pad,[ref]$v) -ne 0){throw 'Controller disconnected.'}
    Start-Sleep -Milliseconds 500
    $v.Left=0;$v.Right=0;[void][ReDUBVibrationTest]::XInputSetState($pad,[ref]$v)
    Start-Sleep -Milliseconds 250
    $v.Right=32767
    if([ReDUBVibrationTest]::XInputSetState($pad,[ref]$v) -ne 0){throw 'Controller disconnected.'}
    Start-Sleep -Milliseconds 500
    Write-Host 'Motor commands succeeded. Confirm that you felt both pulses.'
} finally {
    $v.Left=0;$v.Right=0;[void][ReDUBVibrationTest]::XInputSetState($pad,[ref]$v)
}
