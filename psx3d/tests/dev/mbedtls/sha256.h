/* Host test adapter: production uses devkitPro's mbedTLS. */
#include <openssl/evp.h>
typedef struct {
	EVP_MD_CTX *ctx;
} mbedtls_sha256_context;
static void mbedtls_sha256_init(mbedtls_sha256_context *c) { c->ctx = EVP_MD_CTX_new(); }
static int mbedtls_sha256_starts_ret(mbedtls_sha256_context *c, int sha224)
{
	(void)sha224;
	return EVP_DigestInit_ex(c->ctx, EVP_sha256(), NULL) ? 0 : -1;
}
static int mbedtls_sha256_update_ret(mbedtls_sha256_context *c, const unsigned char *data, size_t n)
{
	return EVP_DigestUpdate(c->ctx, data, n) ? 0 : -1;
}
static int mbedtls_sha256_finish_ret(mbedtls_sha256_context *c, unsigned char *out)
{
	return EVP_DigestFinal_ex(c->ctx, out, NULL) ? 0 : -1;
}
static void mbedtls_sha256_free(mbedtls_sha256_context *c) { EVP_MD_CTX_free(c->ctx); }
