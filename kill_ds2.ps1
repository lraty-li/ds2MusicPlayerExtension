Get-Process | Where-Object {$_.ProcessName -like '*PlayStation*' -or $_.ProcessName -like '*pspc*'} | Stop-Process -Force
Get-Process DS2,'crs-handler' -EA 0 | Stop-Process -Force
Write-Host "All game processes killed"
