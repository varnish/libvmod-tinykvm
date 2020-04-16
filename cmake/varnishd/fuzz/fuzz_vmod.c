#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <openssl/opensslv.h>
#include <openssl/crypto.h>

extern char *
base64_encode(const uint8_t* data, const size_t len);
extern void
init_rsa_key(const char* private_key);
extern int
hmac_sha256_sign(const char* data, size_t len, const char* secret, char* signat);
extern int
rsa_sha256_sign(const char* data, size_t len, char* signat);

const char* jwt_private_key =
"-----BEGIN RSA PRIVATE KEY-----\n"
"MIIEpQIBAAKCAQEA1gybXv3r835mYoyzynXiggvOL3zOVK2ZGTOM4U87CVOIvqo9\n"
"xp/mMtBjRXj/XF8XWAHV0pi0IfL4/FeDRqWkB8VjqOP1DfhSvtroPsrIxTsHXenC\n"
"Jh+JAbj61CfwUDbHIg+31Cj1JHXbXmdtbr+c71ZhtRN2fJstrzFkG0jzt7CQ3HF4\n"
"NjQEM5IrwxY45XCUL73Tt4XnuoLlosb83ZtS4Mi+lsAPA7xUGA9BZngLtqkyjch0\n"
"Ap5e9NWfO8U8XX8Ponp3TmLOC5q8t1EgVHvpSaNLHdqO79tIWMnMaeWk6HjTYC6Y\n"
"6UCXLJ+9Br5wzFftFw4HrAXqKQ1GyIMxNVyLqQIDAQABAoIBABXMPt5TmX24d5C9\n"
"p2mMy75WmW64lQKGkzq/xy8CtI5klV5lU9kwW279Tb67bbXocSYjObUym9WBOt3n\n"
"w5SkNaoc7eM7I6+ChFqvTEtotz1un3du4LilKXyla9XDI3PnwCu10hCnAx/taKOa\n"
"gMhwdvjgoR6hlsVlwCdBLmKg8UDZaJhgnDhcwHjCbawGRwijuQSnQi/4be2hBKpQ\n"
"izffdnnLUYgbuU/xGx8Nwd7i1AiJ/7WgZ9vzbGXpCfN0Icv4O1jgcngBix5thbal\n"
"4YhQjOPQiM2EcDV12RcHZd+qKU1c7OTWsHSzJQr8tKY8Ff2pTLEU7vTkOrIrQ4nn\n"
"YFbmc6ECgYEA7ZsfJjUHTe0DbxQfCItUiPNlbt6JJO9ZONIFj43Alo4cs9QdpF5I\n"
"qkxm+51v0MeQzGxYEXCrHZNMntiDBYnCLvio9j3dMO7On1hPBmHHSF3jpXkZJBbU\n"
"y7oUYOKqOzkEkgLpsLQKYvGTWJhPksJd9K4Op6OQC3qS7fN/VuMN1LUCgYEA5p6j\n"
"YsSb10g19eVb4DnIg4LpvN63pN9NfW5IduKpeIjq+YVxpc6OKoSpY0vOMqxGV5Gr\n"
"Zil9trRo3O0dCS1q0W5fJXLe8bsdHvnCWADlERCrwQdw4GFFJHCpHGTJEcma/KLd\n"
"wGp3uqGgd6ajLcGViTwOfJP17I1C+DPM6kSLh6UCgYEAqp7qtk/8B8w73Abx5fvP\n"
"X2yQmRW6G8i4JCJElfovoq04FTYrdv2xZoDorqQ2SBEWfIUMlLF4XwuISMSnCVIM\n"
"HBi5k+GGtX226BvM24NZTDEHqKqWECFI+2aK/aumnFJsYsEuuJIAp15b9ZGiCnwC\n"
"ZhKbOWwtouCJI8/n0CfJpcUCgYEAxbFv9hb8UMwCFcyLuedO0A3FOLbjTl5uGvwS\n"
"+nbLOByG8WdHSQ+MJz6ZxhkRpbawhhjFiUpADgMRcXGB1oAsdWPcBEo4e5gfGpKX\n"
"2sDJnJth4JL0XCSGFPrOvRxYkPr19WPYHD4obMBowqkCcRLlkUL+WCSuSB3ALuyY\n"
"2KnxwlECgYEAqqSxGR5b0t53ddZ+ByvZJVll/XPqj2UrGlId+dCkh1kfPjFmfiul\n"
"jUJ6J92aU62/OkvgNlwHz1/CRw1xwx6CM7QUKclKE3LDujBJ4iR63Qtep9HbDl5v\n"
"kpRAzQSz2PVhTcIXJEllAtCRexIckjKz1ToHjgmjdpqj7wjWfdEDAg0=\n"
"-----END RSA PRIVATE KEY-----\n";

extern void varnishd_initialize(const char*);
extern int  open_varnishd_connection();

//#define VMOD_COOKIEPLUS
//#define VMOD_URLPLUS
//#define VMOD_WAF
//#define VMOD_JWT
#define VMOD_HEADERPLUS
static const size_t JWT_MAX = 9000;
static const bool do_encode_jwt_token = true;

size_t jwt_encode(const uint8_t* data, const size_t size,
				const char* secret_key, char megabuffer[JWT_MAX])
{
	size_t megalen = 0;
	const char* header = "{\"alg\" : \"HS256\", \"typ\" : \"JTW\"}";
	if (size == 0) return 0;

	// JWT header
	char *encoded_header = base64_encode(header, strlen(header));
	const size_t enc_header_len = strlen(encoded_header);
	memcpy(megabuffer, encoded_header, enc_header_len);
	megalen += enc_header_len;
	megabuffer[megalen++] = '.';

	// JWT bullshit
	char *buffer = base64_encode(data, size);
	memcpy(&megabuffer[megalen], buffer, strlen(buffer));
	megalen += strlen(buffer);
	megabuffer[megalen++] = '.';

	// signature buffer
	const size_t hm_size = enc_header_len + 1 + strlen(buffer);
	char* hm = malloc(hm_size);
	memcpy(hm, encoded_header, enc_header_len);
	hm[enc_header_len] = '.';
	memcpy(&hm[enc_header_len + 1], buffer, strlen(buffer));

	free(encoded_header);
	free(buffer);

	// signature HMAC SHA256
	char sign_hex[512];
	int sign_len = hmac_sha256_sign(hm, hm_size, secret_key, sign_hex);
	assert(sign_len > 0);
	free(hm);
	//fprintf(stderr, "%.*s\n", sign_len, sign_hex);

	char *signature = base64_encode(sign_hex, sign_len);
	memcpy(&megabuffer[megalen], signature, strlen(signature));
	megalen += strlen(signature);
	assert(megalen <= JWT_MAX);
	free(signature);
	//fprintf(stderr, "%.*s\n", megalen, megabuffer);

	return megalen;
}

void vmod_fuzzer(uint8_t* data, size_t len)
{
    static bool init = false;
    if (init == false) {
        init = true;
        varnishd_initialize(
			"/home/gonzo/github/varnish_autoperf/vcl/vmod_fuzz.vcl"
		);
		/*OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS
			 | OPENSSL_INIT_ADD_ALL_DIGESTS
			 | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);*/
		fprintf(stderr, "%s\n", jwt_private_key);
		init_rsa_key(jwt_private_key);
    }
	if (len == 0) return;

    int cfd = open_varnishd_connection();
    if (cfd < 0) {
        fprintf(stderr, "Could not create socket... going to sleep\n");
        sleep(1);
        return;
    }

#ifdef VMOD_COOKIEPLUS
	const char* req = "GET / HTTP/1.1\r\nHost: 127.0.0.1";
	int ret = write(cfd, req, strlen(req));
	if (ret < 0) {
		//printf("Writing the request failed\n");
		close(cfd);
		return;
	}

	const char* seq = "\r\nCookie: test=";
    int ret = write(cfd, seq, strlen(seq));
    if (ret < 0) {
        //printf("Writing the request failed\n");
        close(cfd);
        return;
    }

	ssize_t bytes = write(cfd, data, len);
    if (bytes < 0) {
        close(cfd);
        return;
    }
	write(cfd, "\r\n\r\n", 4);
#elif defined(VMOD_URLPLUS)
	const char* req = "GET /";
	int ret = write(cfd, req, strlen(req));
	if (ret < 0) {
		//printf("Writing the request failed\n");
		close(cfd);
		return;
	}

	ret = write(cfd, data, len);
	if (ret < 0) {
		close(cfd);
		return;
	}
	
	req = " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
	ret = write(cfd, req, strlen(req));
	if (ret < 0) {
		//printf("Writing the request failed\n");
		close(cfd);
		return;
	}
#elif defined(VMOD_WAF)
	char buffer[5000];
	int L = snprintf(buffer, sizeof(buffer),
			"GET /%.*s HTTP/1.1\r\nHost: 127.0.0.1\r\n"
			"Content-Type: text/html\r\n"
			"Content-Length: %zu\r\n"
			"\r\n", (int) len, data, len);
	int ret = write(cfd, buffer, L);
	if (ret < 0) {
		//printf("Writing the request failed\n");
		close(cfd);
		return;
	}

	ssize_t bytes = write(cfd, data, len);
	if (bytes < 0) {
		close(cfd);
		return;
	}
#elif defined(VMOD_JWT)
	const char* req = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nInput: ";
	int ret = write(cfd, req, strlen(req));
	if (ret < 0) {
		//printf("Writing the request failed\n");
		close(cfd);
		return;
	}

	if (do_encode_jwt_token) {
		char megabuffer[JWT_MAX];
		size_t megalen = jwt_encode(data, len, "my secret", megabuffer);
		ssize_t bytes = write(cfd, megabuffer, megalen);
		if (bytes < 0) {
			close(cfd);
			return;
		}
	} else {
		ssize_t bytes = write(cfd, data, len);
		if (bytes < 0) {
			close(cfd);
			return;
		}
	}

	ssize_t bytes = write(cfd, "\r\n\r\n", strlen("\r\n\r\n"));
	if (bytes < 0) {
		close(cfd);
		return;
	}
#elif defined(VMOD_HEADERPLUS)
	const char* req = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nInput: ";
	int ret = write(cfd, req, strlen(req));
	if (ret < 0) {
		//printf("Writing the request failed\n");
		close(cfd);
		return;
	}

	ssize_t bytes = write(cfd, data, len);
	if (bytes < 0) {
		close(cfd);
		return;
	}

	const char* postamble = "\r\n\r\n";
	ret = write(cfd, postamble, strlen(postamble));
	if (ret < 0) {
		close(cfd);
		return;
	}
#else
	const char* req = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nInput: ";
	int ret = write(cfd, req, strlen(req));
	if (ret < 0) {
		//printf("Writing the request failed\n");
		close(cfd);
		return;
	}

	ssize_t bytes = write(cfd, data, len);
	if (bytes < 0) {
		close(cfd);
		return;
	}
#endif

    // signalling end of request, increases exec/s by 4x
    shutdown(cfd, SHUT_WR);

    char readbuf[2048];
    ssize_t rlen = read(cfd, readbuf, sizeof(readbuf));
    if (rlen < 0) {
        // Connection reset by peer just means varnishd closed early
        if (errno != ECONNRESET) {
            printf("Read failed: %s\n", strerror(errno));
        }
        close(cfd);
        return;
    }

    close(cfd);
}
