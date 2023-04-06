cmake_minimum_required (VERSION 3.0.2)
project (vmod C)
include(../vmod.cmake vmod)

set (PATH ${VARNISH_SOURCE_DIR}/lib/libvmod_crypto)

add_vmod(vmod_crypto ${PATH}/vmod_crypto.vcc "Crypto VMOD"
	${PATH}/crypto_aes.c
	${PATH}/crypto_base64.c
	${PATH}/crypto_blob.c
	${PATH}/crypto_cache.c
	${PATH}/crypto_cipher.c
	${PATH}/crypto_cipher_vdp.c
	${PATH}/crypto_cipher_vfp.c
	${PATH}/crypto_hash.c
	${PATH}/crypto_hash_kernel.c
	${PATH}/crypto_hash_ssl.c
	${PATH}/crypto_hex.c
	${PATH}/crypto_hmac.c
	${PATH}/crypto_hmac_ssl.c
	${PATH}/crypto_priv.c
	${PATH}/crypto_random.c
	${PATH}/crypto_secret.c
)
# Le sigh
target_include_directories(vmod_crypto
	PRIVATE ${VARNISH_SOURCE_DIR})

# VCL import name is "crypto"
add_vmod_tests(vmod_crypto crypto
)
