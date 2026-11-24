# Clang 21, shipped as clang-cl with Visual Studio 18, has _m_prefetch as a
# builtin. SDL 2.26.3 defines the same function, both in the RT64 checkout and
# in the SDL copy CMake downloads into cmake-build/_deps. Keep the definition
# for older Clang, where winnt.h still needs it.
$ErrorActionPreference = "Stop"
function Normalize([string]$value) {
    return ($value -replace "`r`n", "`n" -replace "`r", "`n")
}
$old = Normalize @"
static __inline__ void __attribute__((__always_inline__, __nodebug__))
_m_prefetch(void *__P)
{
  __builtin_prefetch(__P, 0, 3 /* _MM_HINT_T0 */);
}
"@
$new = Normalize @"
#if __clang_major__ < 21
static __inline__ void __attribute__((__always_inline__, __nodebug__))
_m_prefetch(void *__P)
{
  __builtin_prefetch(__P, 0, 3 /* _MM_HINT_T0 */);
}
#endif
"@
$files = Get-ChildItem -Path (Get-Location) -Recurse -Filter SDL_endian.h -File -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\.git\\' }
if (-not $files) {
    throw "No SDL_endian.h headers were found"
}
$patched = 0
foreach ($file in $files) {
    $text = Normalize ([System.IO.File]::ReadAllText($file.FullName))
    if ($text.Contains("#if __clang_major__ < 21")) {
        Write-Host "Already guarded $($file.FullName)"
        continue
    }
    if (-not $text.Contains($old)) {
        Write-Host "No _m_prefetch definition in $($file.FullName)"
        continue
    }
    [System.IO.File]::WriteAllText($file.FullName, $text.Replace($old, $new))
    Write-Host "Guarded _m_prefetch in $($file.FullName)"
    $patched++
}
if ($patched -eq 0) {
    Write-Host "No SDL header needed a new guard. The clang-cl build also passes -fno-builtin-_m_prefetch."
}
