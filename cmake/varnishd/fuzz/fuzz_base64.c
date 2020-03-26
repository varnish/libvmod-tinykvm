#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

char*
base64_encode(const uint8_t* data, const size_t len)
{
	BIO *b64 = BIO_new(BIO_f_base64());
	BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

	BIO *bio = BIO_new(BIO_s_mem());
	BIO_push(b64, bio);

	int ret = BIO_write(b64, data, len);
	assert(ret == len);

	ret = BIO_flush(b64);
	assert(ret == 1);

	BUF_MEM *b64_result = NULL;
	BIO_get_mem_ptr(b64, &b64_result);

	char *buffer = malloc(b64_result->length + 1);
	assert(buffer);

	memcpy(buffer, b64_result->data, b64_result->length);
	int i = 0;
	for (; i < b64_result->length; i++) {
		if ('+' == buffer[i])
			buffer[i] = '-';
		else if ('/' == buffer[i])
			buffer[i] = '_';
		else if ('=' == buffer[i]) {
			buffer[i] = '\0';
			break;
		}
	}

	if (i == b64_result->length)
		buffer[b64_result->length] = '\0';

	BIO_free_all(b64);
	return buffer;
}
