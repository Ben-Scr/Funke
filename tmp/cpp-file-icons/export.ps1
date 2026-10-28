$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$repo = 'C:\Users\benso\source\repos\Pending\Index'
$sources = Join-Path $repo 'design\editor-icons\sources\native-script-files'
$runtime = Join-Path $repo 'Funke-Runtime\FunkeAssets\Textures\Editor\FileIcons'
New-Item -ItemType Directory -Force -Path $sources | Out-Null
$inputs = [ordered]@{
    file_cpp = 'C:\Users\benso\.codex\generated_images\01a182fd-9894-7152-9138-83a0e4d76f6a\exec-9b1dadd6-bf48-40dd-b391-10173c99da73.png'
    file_header = 'C:\Users\benso\.codex\generated_images\01a182fd-9894-7152-9138-83a0e4d76f6a\exec-a2f15948-0881-4785-aab4-f14adafc3a76.png'
}
foreach ($name in $inputs.Keys) {
    Copy-Item -LiteralPath $inputs[$name] -Destination (Join-Path $sources "$name.png")
    $master = [System.Drawing.Bitmap]::new($inputs[$name])
    $folder = Join-Path $runtime $name
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    foreach ($size in @(16,32,64,128,256)) {
        $bitmap = [System.Drawing.Bitmap]::new($size,$size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($bitmap)
        $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.DrawImage($master,[System.Drawing.Rectangle]::new(0,0,$size,$size),0,0,$master.Width,$master.Height,[System.Drawing.GraphicsUnit]::Pixel)
        $bitmap.Save((Join-Path $folder "${name}_${size}.png"),[System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose()
        $bitmap.Dispose()
    }
    $master.Dispose()
}
$preview = [System.Drawing.Bitmap]::new(1140,670,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($preview)
$g.Clear([System.Drawing.ColorTranslator]::FromHtml('#17191D'))
$font = [System.Drawing.Font]::new('Segoe UI',13,[System.Drawing.FontStyle]::Regular,[System.Drawing.GraphicsUnit]::Pixel)
$fontTitle = [System.Drawing.Font]::new('Segoe UI',18,[System.Drawing.FontStyle]::Bold,[System.Drawing.GraphicsUnit]::Pixel)
$g.DrawString('C# reference  /  C++ source  /  C++ header', $fontTitle, [System.Drawing.Brushes]::White, 24, 16)
$names = @('file_cs','file_cpp','file_header')
$labels = @('C# existing','C++  .cpp','Header  .h / .hpp')
foreach ($theme in @(0,1)) {
    $y = 55 + $theme * 300
    $bg = if ($theme -eq 0) { '#292C32' } else { '#F2F3F5' }
    $fg = if ($theme -eq 0) { '#E6E8EC' } else { '#252830' }
    $brushBg = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml($bg))
    $brushFg = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml($fg))
    for ($i=0; $i -lt 3; $i++) {
        $x = 20 + $i*373
        $g.FillRectangle($brushBg,$x,$y,356,280)
        $g.DrawString($labels[$i],$fontTitle,$brushFg,($x+16),($y+12))
        foreach ($item in @(@(128,16,52),@(64,167,93),@(32,253,125),@(16,307,141))) {
            $size = $item[0]
            $icon = [System.Drawing.Bitmap]::new((Join-Path $runtime "$($names[$i])\$($names[$i])_${size}.png"))
            $g.DrawImage($icon,[System.Drawing.Rectangle]::new(($x+$item[1]),($y+$item[2]),$size,$size),0,0,$size,$size,[System.Drawing.GraphicsUnit]::Pixel)
            $g.DrawString("$size px",$font,$brushFg,($x+$item[1]),($y+195))
            $icon.Dispose()
        }
    }
    $brushBg.Dispose()
    $brushFg.Dispose()
}
$preview.Save((Join-Path $repo 'tmp\cpp-file-icons\preview.png'),[System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$font.Dispose()
$fontTitle.Dispose()
$preview.Dispose()
Write-Output 'Exported 10 RGBA icons plus original masters and final-size preview.'
