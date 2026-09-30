/* libdatachannel's DTLS implementation requires this extension even when
   media is disabled. Keep the upstream defaults, including verification. */
#ifndef HALO_MBEDTLS_CONFIG_H
#define HALO_MBEDTLS_CONFIG_H
#define MBEDTLS_SSL_DTLS_SRTP
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
#endif
