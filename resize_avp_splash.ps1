param(
    [Parameter(Mandatory=$true)]
    [string]$Source,

    [Parameter(Mandatory=$true)]
    [string]$Destination
)

Add-Type -AssemblyName System.Drawing

$image = [System.Drawing.Image]::FromFile($Source)

try {
    $bitmap = [System.Drawing.Bitmap]::new(400, 240)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)

    try {
        $graphics.Clear([System.Drawing.Color]::Black)
        $graphics.InterpolationMode =
            [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

        $graphics.DrawImage($image, 0, 0, 400, 240)

        $bitmap.Save(
            $Destination,
            [System.Drawing.Imaging.ImageFormat]::Png
        )
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}
finally {
    $image.Dispose()
}

Write-Host "Created $Destination"
