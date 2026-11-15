/* crypto.h — Keringő OS Wi-Fi kripto (SHA1, HMAC, PBKDF2, PRF, AES-CCMP) */
#ifndef KERINGO_CRYPTO_H
#define KERINGO_CRYPTO_H

#include "kernel.h"

/* SHA1 */
void sha1(const u8 *data, u32 len, u8 out[20]);
/* HMAC-SHA1 (RFC 2104) */
void hmac_sha1(const u8 *key, u32 klen, const u8 *data, u32 dlen, u8 mac[20]);
/* PBKDF2-HMAC-SHA1 (RFC 2898) — WPA2 PMK: 4096 iteráció */
void pbkdf2_sha1(const char *pass, const char *ssid, int iters, u8 out[32]);
/* 802.11i PRF (hostap-kompatibilis) — PTK levezetés (4-way kézfogás);
 * pontosan outlen bájtot ír (48 = CCMP PTK) */
void prf512(const u8 *key, u32 klen, const char *label,
            const u8 *data, u32 dlen, u8 *out, u32 outlen);

/* AES-128 csak előre irány (CCM-hez ez elég) */
void aes128_init(u32 rk[44], const u8 key[16]);
void aes128_enc(const u32 rk[44], const u8 in[16], u8 out[16]);

/* CCMP (802.11 CCMP = AES-CCM, L=2, M=8) */
void ccmp_keystream(const u8 key[16], const u8 nonce[13], u16 ctr,
                    u8 *out, u32 len);
void ccmp_mic(const u8 key[16], const u8 nonce[13],
              const u8 *aad, u32 aadlen,
              const u8 *data, u32 dlen, u8 mic[8]);
/* Teljes CCMP keret (MIC ki/beszúrás + titkosítás):
 * in: 802.11 fejléc nélküli "payload" (AAD-t külön kapjuk),
 * out ugyanakkora mint in.  vissza: 1 OK */
int ccmp_encrypt(const u8 key[16], const u8 nonce[13],
                 const u8 *aad, u32 aadlen,
                 const u8 *plain, u32 plen, u8 *out);   /* plen+8 bájtra ír (MIC) */
int ccmp_decrypt(const u8 key[16], const u8 nonce[13],
                 const u8 *aad, u32 aadlen,
                 const u8 *cipher, u32 clen, u8 *out);  /* clen tartalmazza a MIC-et */

/* Boot-idő önteszt ismert vektorokkal; 1 = minden OK (soros kimenetre ír) */
int crypto_self_test(void);

#endif
