The Lua 5.4.8 port is preserved from microsoft/vcpkg commit
`8df214f8617c546a819b0a29430a1cd1388d4204` (MIT, see lua/COPYRIGHT).
The bundled sol2 does not support Lua 5.5. Other dependencies use vcpkg 2026.10.16.

The kubazip 0.3.15 port comes from vcpkg 2026.10.16 (MIT). Its source archive
uses the immutable commit behind upstream tag v0.3.15. The archive has the same
files as the original vcpkg download and retains SHA-512 verification.
