/* What the NFC tag holds and how it answers a reader: an NDEF URI record in
 * NFC Forum Type 2 Tag memory, read-only. No hardware here; nfc_tag.c
 * drives the NFCT peripheral with it. */
#ifndef TAPUI_NDEF_H
#define TAPUI_NDEF_H

#include <stddef.h>
#include <stdint.h>

#define T2T_UID_LEN 7
#define T2T_READ_LEN 16

/* A one-record NDEF message holding uri. Returns its length, or -ENOSPC. */
int ndef_uri_message(const char *uri, uint8_t *out, size_t cap);

/* Lays out tag memory: UID + lock pages, capability container, then the
 * message in an NDEF TLV. Returns the memory length (a multiple of 4), or
 * -ENOSPC. */
int t2t_build(const uint8_t uid[T2T_UID_LEN], const uint8_t *ndef, size_t ndef_len,
	      uint8_t *mem, size_t cap);

enum t2t_action {
	T2T_REPLY,  /* send reply[0..15] */
	T2T_SLEEP,  /* HLTA: go to sleep, no reply */
	T2T_IGNORE, /* anything else: stay silent and keep listening */
};

enum t2t_action t2t_handle(const uint8_t *mem, size_t mem_len, const uint8_t *cmd,
			   size_t cmd_len, uint8_t reply[T2T_READ_LEN]);

#endif
