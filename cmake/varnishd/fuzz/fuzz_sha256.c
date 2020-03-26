#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <assert.h>
#include <string.h>
static RSA *rsa_key = NULL;

void init_rsa_key(const char* private_key)
{
	BIO *bio = BIO_new_mem_buf((const void *) private_key, strlen(private_key));
	//BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
	assert(bio);

	PEM_read_bio_RSAPrivateKey(bio, &rsa_key, 0, NULL);
	ERR_print_errors_fp(stderr);
	assert(rsa_key);
	BIO_free_all(bio);
}

int hmac_sha256_sign(const uint8_t* data, size_t len, 
					const char* secret, uint8_t* signbuf)
{
	const EVP_MD *md = EVP_get_digestbyname("sha256");
	assert(md != NULL);

	HMAC_CTX *hmac = HMAC_CTX_new();
	HMAC_Init_ex(hmac, secret, strlen(secret), md, NULL);
	HMAC_Update(hmac, data, len);

	int sign_len = 0;
	HMAC_Final(hmac, signbuf, &sign_len);
	HMAC_CTX_free(hmac);
	return sign_len;
}

int rsa_sha256_sign(const char* data, size_t len, uint8_t* signbuf)
{
	EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
	const EVP_MD *md = EVP_get_digestbyname("SHA256");

	// create digest
	EVP_DigestInit_ex(mdctx, md, NULL);
	EVP_DigestUpdate(mdctx, (const void*) data, len);

	uint8_t digest_data[SHA512_DIGEST_LENGTH];
	unsigned digest_len = 0;
	EVP_DigestFinal_ex(mdctx, digest_data, &digest_len);
	EVP_MD_CTX_free(mdctx);

	// sign the digest
	int signlen = 0;
	RSA_sign(NID_sha256, digest_data, digest_len, 
			 signbuf, &signlen, rsa_key);
	return signlen;
}
