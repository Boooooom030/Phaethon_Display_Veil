Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000} -MaxEvents 6 |
  ForEach-Object {
    $x = [xml] $_.ToXml()
    $d = $x.Event.EventData.Data
    $app = ($d | Where-Object { $_.Name -eq 'AppName' }).'#text'
    if (-not $app) { $app = ($d[0].'#text') }
    $mod = ($d | Where-Object { $_.Name -eq 'FaultingModule' }).'#text'
    if (-not $mod) { $mod = ($d[3].'#text') }
    $off = ($d | Where-Object { $_.Name -eq 'FaultingOffset' }).'#text'
    if (-not $off) { $off = ($d[6].'#text') }
    $code = ($d | Where-Object { $_.Name -eq 'ExceptionCode' }).'#text'
    if (-not $code) { $code = ($d[5].'#text') }
    Write-Output ("APP=" + $app + " MODULE=" + $mod + " EXC=" + $code + " RVA=" + $off)
  }
