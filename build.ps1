# Compiles the mod with Windhawk's own toolchain and flags, outside the GUI.
#
# Usage:  pwsh -File build.ps1
#
# This exists because iterating through Windhawk's editor means a click per
# attempt. It uses the exact flags Windhawk itself passes (read from
# EditorWorkspace\compile_flags.txt), so a pass here means a pass there.
$ErrorActionPreference = 'Stop'

$compiler = 'D:\Program Files\Windhawk\Compiler'
$windhawkData = 'C:\ProgramData\Windhawk'
$source = Join-Path $PSScriptRoot 'endfield-info-windhawk.wh.cpp'
$flagsFile = Join-Path $windhawkData 'EditorWorkspace\compile_flags.txt'
$output = Join-Path $env:TEMP 'endfield-info-windhawk.dll'

foreach ($p in @($compiler, $source)) {
    if (-not (Test-Path -LiteralPath $p)) { throw "not found: $p" }
}

if (Test-Path -LiteralPath $flagsFile) {
    $flags = (Get-Content -LiteralPath $flagsFile) -join ' '
} else {
    # Same flags Windhawk uses, inlined so the script still works after a clean install.
    $flags = @(
        '-x','c++','-std=c++23','-target','x86_64-w64-mingw32',
        '-DUNICODE','-D_UNICODE','-DWINVER=0x0A00','-D_WIN32_WINNT=0x0A00','-D_WIN32_IE=0x0A00',
        '-DNTDDI_VERSION=0x0A000008','-D__USE_MINGW_ANSI_STDIO=0','-DWH_MOD','-DWH_EDITING',
        '-include','windhawk_api.h','-Wall','-Wextra','-Wno-unused-parameter',
        '-Wno-missing-field-initializers','-Wno-cast-function-type-mismatch'
    ) -join ' '
}

# NOTE: this mod deliberately avoids new/delete, std::* and exceptions.
# That is a *size* decision, not a loadability one: Windhawk ships and injects
# libc++.whl (Engine\Mods\<arch>\libc++.whl), so a mod importing it does run.
# Steering clear of heap allocation keeps the DLL at ~122 KB instead of ~578 KB
# with "-static-libstdc++ -static-libgcc".
$libs = '-shared -O2 -luser32 -lgdi32 -lpdh -liphlpapi -lpsapi -ladvapi32'
$command = "`"$compiler\bin\clang++.exe`" $flags $libs -o `"$output`" `"$source`""

Write-Host "compiling $source" -ForegroundColor Cyan
cmd /c "$command 2>&1"
$exit = $LASTEXITCODE

if ($exit -eq 0 -and (Test-Path -LiteralPath $output)) {
    $dll = Get-Item -LiteralPath $output
    Write-Host ("OK  {0} bytes -> {1}" -f $dll.Length, $dll.FullName) -ForegroundColor Green
    $objdump = 'D:\Program Files\LLVM\bin\llvm-objdump.exe'
    if (Test-Path -LiteralPath $objdump) {
        Write-Host 'exports:' -ForegroundColor Cyan
        & $objdump -p $output 2>&1 | Select-String -Pattern 'Wh_Mod|WhTool_' |
            ForEach-Object { '  ' + $_.Line.Trim() }
    }
} else {
    Write-Host "FAILED (exit $exit)" -ForegroundColor Red
    exit 1
}
