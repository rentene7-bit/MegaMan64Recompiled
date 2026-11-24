# Clang 21, shipped as clang-cl with Visual Studio 18, has _m_prefetch as a
# builtin. SDL 2.26.3 defines the same function so the Windows build stops in
# SDL_endian.h. Keep the definition for older Clang, where winnt.h still needs it.
$ErrorActionPreference = "Stop"
$path = Join-Path (Get-Location) "lib\rt64\src\contrib\mupen64plus-win32-deps\SDL2-2.26.3\include\SDL_endian.h"
if (-not (Test-Path $path)) {
    throw "SDL_endian.h was not checked out at $path"
}
$text = [System.IO.File]::ReadAllText($path)
if ($text.Contains("#if __clang_major__ < 21")) {
    Write-Host "SDL_endian.h already skips the _m_prefetch builtin"
    exit 0
}
$old = @"
static __inline__ void __attribute__((__always_inline__, __nodebug__))
_m_prefetch(void *__P)
{
  __builtin_prefetch(__P, 0, 3 /* _MM_HINT_T0 */);
}
"@
$new = @"
#if __clang_major__ < 21
static __inline__ void __attribute__((__always_inline__, __nodebug__))
_m_prefetch(void *__P)
{
  __builtin_prefetch(__P, 0, 3 /* _MM_HINT_T0 */);
}
#endif
"@
$updated = $text.Replace($old, $new)
if ($updated -eq $text) {
    throw "SDL_endian.h did not contain the expected _m_prefetch definition"
}
[System.IO.File]::WriteAllText($path, $updated)
Write-Host "Guarded _m_prefetch in SDL_endian.h"
