/* Developer benchmark provisioning. Never replace a numbered save state. */
#include "benchstate.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

bool bench_state_name(const char *name)
{
    /* Defense in depth: original six are load-only even when a slot is absent.
       Prefixes cover regions/revisions and GT2's Arcade/Simulation discs. */
    static const char *const protected[] = {
        "Crash Bandicoot (", "CTR - Crash Team Racing", "Crash Team Racing",
        "Gran Turismo 2", "Silent Hill", "R4 - Ridge Racer Type 4",
        "Ridge Racer Type 4", "Castlevania - Symphony of the Night"
    };
    if (!name || !*name || strlen(name) > 200 || strstr(name, "..")) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p < 32 || *p == 127 || strchr("/\\:\"*?<>|", *p)) return false;
    for (unsigned i = 0; i < sizeof(protected)/sizeof(*protected); i++)
        if (!strncasecmp(name, protected[i], strlen(protected[i]))) return false;
    return strcasecmp(name, "Crash Bandicoot") != 0;
}
bool bench_state_header(const unsigned char *data, size_t size)
{
    /* Native libretro raw state, current SaveVersion, HLE BIOS. This is an
       envelope check, not a claim to validate every core-internal structure. */
    static const unsigned char version[4] = { 6, 0, 0x41, 0x8b };
    static const unsigned char hle[4] = { 1, 0, 0, 0 };
    return size == BENCH_STATE_BYTES && !memcmp(data,"STv4 PCSX",9) &&
        !memcmp(data+32,version,4) && !memcmp(data+36,hle,4);
}
bool bench_state_commit(const char *staged, const char *destination)
{
    FILE *in = fopen(staged,"rb");
    if (!in) return false;
    /* O_EXCL also protects a slot created while the network transfer was in
       progress. Do not rely on rename-over-existing semantics (FAT vs POSIX). */
    int fd = open(destination,O_CREAT|O_EXCL|O_WRONLY,0600);
    if (fd < 0) { fclose(in); return false; }
    FILE *out = fdopen(fd,"wb");
    if (!out) { close(fd); fclose(in); unlink(destination); return false; }
    unsigned char buffer[8192];
    size_t total = 0, n;
    bool ok = true;
    while ((n = fread(buffer,1,sizeof(buffer),in))) {
        if (fwrite(buffer,1,n,out) != n) { ok = false; break; }
        total += n;
    }
    if (ferror(in) || total != BENCH_STATE_BYTES) ok = false;
    fclose(in);
    if (fclose(out)) ok = false;
    if (!ok) unlink(destination); /* only the file we exclusively created */
    return ok;
}
