# Compatibility entry point for the runtime UI coverage checker.
param([string]$FontFile = "", [string]$LangFile = "")
$ErrorActionPreference = "Stop"
$checkArgs = @((Join-Path $PSScriptRoot 'check_cjk_coverage.py'))
if ($FontFile) { $checkArgs += @('--font', $FontFile) }
if ($LangFile) { $checkArgs += @('--lang', $LangFile) }
& python @checkArgs
exit $LASTEXITCODE
