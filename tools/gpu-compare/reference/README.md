# Host-only renderer references

These portable P.E.Op.S. and NEON/C renderer files came from PCSX-ReARMed
`c8816799b50388e61cfe237fe2cdbb7d8175f20a`. They are independent test oracles
for the PICA regression harness and are not part of the 3DS core or build.
Only the compilation units/includes used by `../build.sh` are kept; original
source bytes, GPL notices and credits are unchanged. The script assembles a
private reference tree with the current shared core headers.

See [PCSX-ReARMed credits](../../../core/AUTHORS) and [GPL](../../../core/COPYING).
