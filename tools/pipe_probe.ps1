param([string]$Cmd = 'STATUS')
$pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'SunshinePrivacyScreen', [System.IO.Pipes.PipeDirection]::InOut)
try {
    $pipe.Connect(3000)
} catch {
    Write-Output 'PIPE_CONNECT_FAILED'
    exit 2
}
$writer = New-Object System.IO.StreamWriter($pipe)
$writer.NewLine = "`n"
$writer.WriteLine($Cmd)
$writer.Flush()
$reader = New-Object System.IO.StreamReader($pipe)
$out = $reader.ReadToEnd()
Write-Output '--- server response ---'
Write-Output $out
