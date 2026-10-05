$ErrorActionPreference='Stop'
$scriptPath=Join-Path $PSScriptRoot '../tools/windows/Collect-FleaByteDiagnostics.ps1'
$tokens=$null;$errors=$null
[Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$errors)|Out-Null
if($errors.Count){throw ($errors.Message -join ';')}
. $scriptPath
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('fleabyte-log-test-'+[guid]::NewGuid().ToString('N'))
try {
  $card=Join-Path $fixture 'card';$logs=Join-Path $card 'FLEABYTE-DIAGNOSTICS';$output=Join-Path $fixture 'output'
  New-Item -ItemType Directory -Path $logs -Force|Out-Null
  $current=Join-Path $logs 'hotspot.log'
  Set-Content -LiteralPath $current -Value 'Unrelated file'
  if(@(Find-DiagnosticLog @($card)).Count){throw 'Collector accepted an unrelated file'}
  Set-Content -LiteralPath $current -Value "FLEABYTE_DIAGNOSTICS_V1`n1 SESSION test`n2 SESSION_END"
  Set-Content -LiteralPath (Join-Path $logs 'hotspot.previous.log') -Value 'previous session'
  if(@(Find-DiagnosticLog @($card)).Count -ne 1){throw 'Valid SD log was not found'}
  # Keep host inspection mocked; this test never changes or inspects networking.
  function Save-WindowsDiagnostics([string]$Destination){Set-Content -LiteralPath (Join-Path $Destination 'windows.txt') -Value 'mock Windows status'}
  $folder=Collect-DiagnosticLog $current $output
  if(-not (Test-Path (Join-Path $folder 'hotspot.previous.log'))){throw 'Previous session not copied'}
  if((Get-Content (Join-Path $folder 'hotspot.log') -Raw) -ne (Get-Content $current -Raw)){throw 'Copied log changed'}
  if(-not (Test-Path (Join-Path $folder 'windows.txt'))){throw 'Windows snapshot missing'}
  $panic=Join-Path $logs 'panic.bin'
  $bytes=New-Object byte[] 36
  [BitConverter]::GetBytes([uint32]36).CopyTo($bytes,0)
  $bytes[24]=127;$bytes[25]=69;$bytes[26]=76;$bytes[27]=70
  [IO.File]::WriteAllBytes($panic,$bytes)
  $folder=Collect-DiagnosticLog $current $output
  if(-not (Test-Path (Join-Path $folder 'panic.bin'))){throw 'Valid crash dump was not copied'}
  $bytes[0]=35; [IO.File]::WriteAllBytes($panic,$bytes)
  $folder=Collect-DiagnosticLog $current $output
  if(Test-Path (Join-Path $folder 'panic.bin')){throw 'Incomplete dump was copied'}
  $bytes[0]=36;$bytes[24]=0; [IO.File]::WriteAllBytes($panic,$bytes)
  $folder=Collect-DiagnosticLog $current $output
  if(Test-Path (Join-Path $folder 'panic.bin')){throw 'Unrelated binary was copied'}
  Write-Host 'PASS: collector syntax, log identity, current/previous session copies and report output'
} finally {
  $resolvedFixture=[IO.Path]::GetFullPath($fixture)
  $resolvedTemp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
  if(-not $resolvedFixture.StartsWith($resolvedTemp,[StringComparison]::OrdinalIgnoreCase) -or
     [IO.Path]::GetFileName($resolvedFixture) -notmatch '^fleabyte-log-test-[0-9a-f]{32}$') { throw 'Unsafe test cleanup path' }
  if(Test-Path -LiteralPath $resolvedFixture){Remove-Item -LiteralPath $resolvedFixture -Recurse -Force}
}
