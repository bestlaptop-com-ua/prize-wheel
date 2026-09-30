$W='C:\Users\Mill\Desktop\prize-wheel\work'; $D="$W\party12_20260930"; $M="$W\monitor"; $log="$D\chain.log"
function L($t){ Add-Content $log "$(Get-Date -Format HH:mm:ss) $t" }
$dead=(Get-Date).AddMinutes(15)
while(-not (Test-Path "$D\compile.exit")){ if((Get-Date) -gt $dead){ L 'compile timeout'; exit }; Start-Sleep 5 }
$ce=(Get-Content "$D\compile.exit").Trim(); $warn=@(Select-String -Path "$D\compile.log" -Pattern 'prize_wheel_gpt\\[^:]*:\d+:\d+: (warning|error)' -CaseSensitive).Count
L "compile.exit=$ce sketch warnings=$warn"
if($ce -ne '0' -or $warn -ne 0){ L 'not uploading'; exit }
Stop-ScheduledTask -TaskName PW_SerialMonitor -ErrorAction SilentlyContinue
Get-CimInstance Win32_Process -Filter "Name='python.exe'" | Where-Object { $_.CommandLine -like '*monitor.py*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Start-Sleep 2
$env:ARDUINO_DIRECTORIES_DATA='C:\Users\Mill\AppData\Local\Arduino15'; $env:ARDUINO_DIRECTORIES_USER='C:\Users\Mill\Documents\Arduino'; $env:ARDUINO_DIRECTORIES_DOWNLOADS='C:\Users\Mill\AppData\Local\Arduino15\staging'
L 'upload start'
& 'C:\Users\Mill\AppData\Local\Programs\Python\Python312\python.exe' "$D\upload_candidate.py"
L ("upload.exit=" + (Get-Content "$D\upload.exit"))
Start-ScheduledTask -TaskName PW_SerialMonitor; Start-Sleep 6
foreach($c in '?','s','f'){ [IO.File]::WriteAllText("$M\cmd.txt",$c); Start-Sleep 3 }
Get-Content "$M\monitor.log" -Tail 60 | Select-String -Pattern '# build:|# state=|# friction' -CaseSensitive | Select-Object -Last 3 | ForEach-Object { L $_.Line.Substring(0,[Math]::Min(200,$_.Line.Length)) }
L 'done'