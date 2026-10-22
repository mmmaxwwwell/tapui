#include "ndef.h"

#include <errno.h>
#include <string.h>

/* NFC Forum URI RTD abbreviations; longest first so "https://www." wins
 * over "https://". */
static const struct {
	uint8_t code;
	const char *prefix;
} uri_prefixes[] = {
	{0x02, "https://www."},
	{0x01, "http://www."},
	{0x04, "https://"},
	{0x03, "http://"},
};

int ndef_uri_message(const char *uri, uint8_t *out, size_t cap)
{
	uint8_t code = 0x00;

	for (size_t i = 0; i < sizeof(uri_prefixes) / sizeof(uri_prefixes[0]); i++) {
		size_t plen = strlen(uri_prefixes[i].prefix);

		if (strncmp(uri, uri_prefixes[i].prefix, plen) == 0) {
			code = uri_prefixes[i].code;
			uri += plen;
			break;
		}
	}

	size_t rest = strlen(uri);
	size_t payload = 1 + rest;

	/* Short record only: a 255-byte payload is far more than a link needs. */
	if (payload > 255 || 4 + payload > cap) {
		return -ENOSPC;
	}

	out[0] = 0xD1; /* MB | ME | SR | TNF well-known */
	out[1] = 1;    /* type length */
	out[2] = (uint8_t)payload;
	out[3] = 'U';
	out[4] = code;
	memcpy(&out[5], uri, rest);
	return (int)(4 + payload);
}

int t2t_build(const uint8_t uid[T2T_UID_LEN], const uint8_t *ndef, size_t ndef_len,
	      uint8_t *mem, size_t cap)
{
	size_t tlv_head = ndef_len < 0xFF ? 2 : 4;
	size_t data = tlv_head + ndef_len + 1; /* + terminator TLV */

	/* The capability container counts the data area in 8-byte units. */
	data = (data + 7) & ~(size_t)7;
	if (data / 8 > 0xFF || 16 + data > cap) {
		return -ENOSPC;
	}

	memset(mem, 0, 16 + data);

	mem[0] = uid[0];
	mem[1] = uid[1];
	mem[2] = uid[2];
	mem[3] = 0x88 ^ uid[0] ^ uid[1] ^ uid[2]; /* BCC0, cascade tag included */
	mem[4] = uid[3];
	mem[5] = uid[4];
	mem[6] = uid[5];
	mem[7] = uid[6];
	mem[8] = uid[3] ^ uid[4] ^ uid[5] ^ uid[6]; /* BCC1 */
	mem[9] = 0x48;
	mem[10] = 0xFF; /* static lock bytes: every page locked */
	mem[11] = 0xFF;

	mem[12] = 0xE1; /* NDEF magic */
	mem[13] = 0x10; /* mapping version 1.0 */
	mem[14] = (uint8_t)(data / 8);
	mem[15] = 0x0F; /* read: always; write: never */

	uint8_t *p = &mem[16];

	*p++ = 0x03; /* NDEF message TLV */
	if (tlv_head == 2) {
		*p++ = (uint8_t)ndef_len;
	} else {
		*p++ = 0xFF;
		*p++ = (uint8_t)(ndef_len >> 8);
		*p++ = (uint8_t)ndef_len;
	}
	memcpy(p, ndef, ndef_len);
	p += ndef_len;
	*p = 0xFE; /* terminator TLV */

	return (int)(16 + data);
}

enum t2t_action t2t_handle(const uint8_t *mem, size_t mem_len, const uint8_t *cmd,
			   size_t cmd_len, uint8_t reply[T2T_READ_LEN])
{
	if (cmd_len == 2 && cmd[0] == 0x30) {
		size_t start = (size_t)cmd[1] * 4;

		if (start >= mem_len) {
			return T2T_IGNORE;
		}
		/* A read running off the end wraps to page 0, as Type 2 tags do. */
		for (size_t i = 0; i < T2T_READ_LEN; i++) {
			reply[i] = mem[(start + i) % mem_len];
		}
		return T2T_REPLY;
	}

	if (cmd_len == 2 && cmd[0] == 0x50 && cmd[1] == 0x00) {
		return T2T_SLEEP;
	}

	return T2T_IGNORE;
}
