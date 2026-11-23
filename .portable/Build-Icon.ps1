$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$repo=Split-Path -Parent $PSScriptRoot
$art=Join-Path $repo 'launcher\art'
$source=Join-Path $art 'icon.png'
$target=Join-Path $art 'redub.ico'
if(!(Test-Path -LiteralPath $source)){throw 'The prepared ReDUB logo is missing.'}
$original=[Drawing.Image]::FromFile($source)
$frames=New-Object 'System.Collections.Generic.List[byte[]]'
$sizes=@(16,24,32,48,64,128,256)
try {
 foreach($size in $sizes){
  $bitmap=[Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $graphics=[Drawing.Graphics]::FromImage($bitmap)
  $stream=[IO.MemoryStream]::new()
  try {
   $graphics.Clear([Drawing.Color]::Transparent)
   $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
   $graphics.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::HighQuality
   $padding=[Math]::Max(1,[int]($size/32))
   $scale=[Math]::Min(($size-2*$padding)/$original.Width,($size-2*$padding)/$original.Height)
   $w=[single]($original.Width*$scale);$h=[single]($original.Height*$scale)
   $rect=[Drawing.RectangleF]::new([single](($size-$w)/2),[single](($size-$h)/2),$w,$h)
   $graphics.DrawImage($original,$rect)
   $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
   $frames.Add($stream.ToArray())
  } finally {$stream.Dispose();$graphics.Dispose();$bitmap.Dispose()}
 }
 $stream=[IO.MemoryStream]::new();$writer=[IO.BinaryWriter]::new($stream)
 try {
  $writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$sizes.Count)
  $offset=6+16*$sizes.Count
  for($i=0;$i -lt $sizes.Count;$i++){
   $dimension=if($sizes[$i] -eq 256){0}else{$sizes[$i]}
   $writer.Write([byte]$dimension);$writer.Write([byte]$dimension);$writer.Write([byte]0);$writer.Write([byte]0)
   $writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$frames[$i].Length);$writer.Write([uint32]$offset)
   $offset+=$frames[$i].Length
  }
  foreach($frame in $frames){$writer.Write([byte[]]$frame)}
  $writer.Flush();[IO.File]::WriteAllBytes($target,$stream.ToArray())
 }finally{$writer.Dispose();$stream.Dispose()}
}finally{$original.Dispose()}
Write-Host 'ReDUB icon ready: 16, 24, 32, 48, 64, 128 and 256 pixels.'
