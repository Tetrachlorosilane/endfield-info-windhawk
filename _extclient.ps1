# A second, independent client for the external-data pipe.
#
# Why this exists: _extclient.py goes through CPython's open()/FileIO. When the mod's
# log shows a connection whose pipe buffer stays empty, there are two candidate
# causes -- "the client never wrote" and "the transport is broken" -- and a client
# that shares no code with the first one is what tells them apart.
#
# The server side reports the outcome, so this only writes: replies show up in the
# Windhawk log as "EXT hello ...", "EXT set ..." or "EXT err code=...".
#
# Usage:
#   pwsh -File _extclient.ps1
#   pwsh -File _extclient.ps1 -Key progress -Ttl 60000 -Text 0.42
#   pwsh -File _extclient.ps1 -Raw 'GARBAGE'          # to see an error code

param(
    [string]$Provider = 'music',
    [string]$Key = 'title',
    [int]$Ttl = 600000,
    [string]$Text = 'Now Playing: Blue Hour',
    [string]$Raw = '',
    [switch]$Event
)

$ErrorActionPreference = 'Stop'
$name = 'endfield-info-windhawk'

$lines = if ($Raw) { @($Raw) } elseif ($Event) {
    # In event mode -Text is the card's title and -Key is unused.
    @("HELLO 1 $Provider", "EVENT $Provider info $Ttl $Text")
} else {
    @("HELLO 1 $Provider", "SET $Provider $Key $Ttl $Text")
}

$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
    '.', $name, [System.IO.Pipes.PipeDirection]::InOut)
try {
    $pipe.Connect(2000)
} catch {
    Write-Output "could not connect to \\.\pipe\$name : $($_.Exception.Message)"
    Write-Output "the mod only creates it when 'External data' is On and a row contains {ext.provider.key}"
    exit 1
}

$writer = [System.IO.StreamWriter]::new($pipe, [System.Text.UTF8Encoding]::new($false))
$writer.AutoFlush = $true
foreach ($line in $lines) {
    Write-Output "-> $line"
    $writer.WriteLine($line)     # one WriteFile per line: the protocol is message mode
}
Start-Sleep -Milliseconds 300    # let the server log what it received
$writer.Dispose()
$pipe.Dispose()
Write-Output "sent $($lines.Count) frame(s); check the Windhawk log for EXT hello / EXT set / EXT err"
