#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "ndef.h"
#include "proto.h"

/* --- line reassembly --- */

static char got[4][TAPUI_LINE_MAX];
static int ngot;
static int ntoolong;

static void collect(const char *line, void *ctx)
{
	ARG_UNUSED(ctx);
	if (!line) {
		ntoolong++;
		return;
	}
	strcpy(got[ngot++], line);
}

static void feed(struct tapui_linebuf *lb, const char *s)
{
	tapui_linebuf_feed(lb, (const uint8_t *)s, strlen(s), collect, NULL);
}

static void reset(void *f)
{
	ARG_UNUSED(f);
	ngot = 0;
	ntoolong = 0;
}

ZTEST_SUITE(lines, NULL, NULL, reset, NULL, NULL);

ZTEST(lines, test_split_across_writes)
{
	struct tapui_linebuf lb = {0};

	feed(&lb, "set bri");
	zassert_equal(ngot, 0);
	feed(&lb, "ght 40\nhel");
	feed(&lb, "lo\r\n");
	zassert_equal(ngot, 2);
	zassert_str_equal(got[0], "set bright 40");
	zassert_str_equal(got[1], "hello");
}

ZTEST(lines, test_too_long_is_dropped_and_reported)
{
	struct tapui_linebuf lb = {0};
	char big[TAPUI_LINE_MAX + 10];

	memset(big, 'x', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	feed(&lb, big);
	feed(&lb, "\npress blink\n");
	zassert_equal(ntoolong, 1);
	zassert_equal(ngot, 1);
	zassert_str_equal(got[0], "press blink");
}

ZTEST(lines, test_longest_allowed_line_fits)
{
	struct tapui_linebuf lb = {0};
	char line[TAPUI_LINE_MAX + 1];

	memset(line, 'y', TAPUI_LINE_MAX - 1);
	line[TAPUI_LINE_MAX - 1] = '\n';
	line[TAPUI_LINE_MAX] = '\0';
	feed(&lb, line);
	zassert_equal(ntoolong, 0);
	zassert_equal(strlen(got[0]), TAPUI_LINE_MAX - 1);
}

/* --- commands --- */

ZTEST_SUITE(commands, NULL, NULL, NULL, NULL, NULL);

ZTEST(commands, test_parse)
{
	struct tapui_cmd c;
	char a[] = "hello";
	char b[] = "press blink";
	char d[] = "set color warm white";
	char e[] = "set note ";

	zassert_ok(tapui_cmd_parse(a, &c));
	zassert_equal(c.op, TAPUI_OP_HELLO);

	zassert_ok(tapui_cmd_parse(b, &c));
	zassert_equal(c.op, TAPUI_OP_PRESS);
	zassert_str_equal(c.id, "blink");

	zassert_ok(tapui_cmd_parse(d, &c));
	zassert_equal(c.op, TAPUI_OP_SET);
	zassert_str_equal(c.id, "color");
	zassert_str_equal(c.value, "warm white");

	zassert_ok(tapui_cmd_parse(e, &c));
	zassert_str_equal(c.value, "");
}

ZTEST(commands, test_reject)
{
	struct tapui_cmd c;
	char *bad[] = {"", "reboot", "hello there", "press", "press a b", "set light", "set"};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		char buf[32];

		strcpy(buf, bad[i]);
		zassert_equal(tapui_cmd_parse(buf, &c), -EINVAL, "accepted \"%s\"", bad[i]);
	}
}

ZTEST(commands, test_json_str)
{
	char out[32];

	zassert_equal(tapui_json_str(out, sizeof(out), "a\"b\\c\n"), 15);
	zassert_str_equal(out, "\"a\\\"b\\\\c\\u000a\"");
	zassert_equal(tapui_json_str(out, sizeof(out), "°C"), 5);
	zassert_str_equal(out, "\"°C\"");
	/* Truncates but still says how much it needed. */
	zassert_equal(tapui_json_str(out, 4, "abcdef"), 8);
	zassert_str_equal(out, "\"ab");
}

/* --- NFC tag --- */

ZTEST_SUITE(nfc, NULL, NULL, NULL, NULL, NULL);

static const char url[] = "https://mmmaxwwwell.github.io/tapui/?n=TapUI-1A2B";

ZTEST(nfc, test_uri_record)
{
	uint8_t m[128];
	int n = ndef_uri_message(url, m, sizeof(m));
	size_t rest = strlen(url) - strlen("https://");

	zassert_equal(n, 5 + rest);
	zassert_equal(m[0], 0xD1);
	zassert_equal(m[1], 1);
	zassert_equal(m[2], 1 + rest);
	zassert_equal(m[3], 'U');
	zassert_equal(m[4], 0x04, "https:// abbreviates to 0x04");
	zassert_mem_equal(&m[5], url + 8, rest);

	n = ndef_uri_message("https://www.example.com", m, sizeof(m));
	zassert_equal(m[4], 0x02);
	n = ndef_uri_message("tel:123", m, sizeof(m));
	zassert_equal(m[4], 0x00);
	zassert_mem_equal(&m[5], "tel:123", 7);

	zassert_equal(ndef_uri_message(url, m, 20), -ENOSPC);
}

static const uint8_t uid[T2T_UID_LEN] = {0x5F, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

ZTEST(nfc, test_tag_memory)
{
	uint8_t ndef[128];
	uint8_t mem[256];
	int nlen = ndef_uri_message(url, ndef, sizeof(ndef));
	int len = t2t_build(uid, ndef, nlen, mem, sizeof(mem));

	zassert_true(len > 0);
	zassert_equal(len % 8, 0);
	zassert_equal(mem[3], 0x88 ^ 0x5F ^ 0x11 ^ 0x22, "BCC0");
	zassert_equal(mem[8], 0x33 ^ 0x44 ^ 0x55 ^ 0x66, "BCC1");
	zassert_equal(mem[12], 0xE1);
	zassert_equal(mem[14] * 8, len - 16, "CC counts the data area");
	zassert_equal(mem[15], 0x0F, "read-only");
	zassert_equal(mem[16], 0x03);
	zassert_equal(mem[17], nlen);
	zassert_mem_equal(&mem[18], ndef, nlen);
	zassert_equal(mem[18 + nlen], 0xFE);
}

ZTEST(nfc, test_long_tlv_form)
{
	static uint8_t ndef[300];
	static uint8_t mem[512];
	int len = t2t_build(uid, ndef, sizeof(ndef), mem, sizeof(mem));

	zassert_true(len > 0);
	zassert_equal(mem[17], 0xFF);
	zassert_equal((mem[18] << 8) | mem[19], 300);
	zassert_equal(t2t_build(uid, ndef, sizeof(ndef), mem, 64), -ENOSPC);
}

ZTEST(nfc, test_reader_commands)
{
	uint8_t ndef[128];
	uint8_t mem[256];
	uint8_t reply[T2T_READ_LEN];
	int len = t2t_build(uid, ndef, ndef_uri_message(url, ndef, sizeof(ndef)), mem, sizeof(mem));
	int last_page = len / 4 - 1;

	zassert_equal(t2t_handle(mem, len, (uint8_t[]){0x30, 3}, 2, reply), T2T_REPLY);
	zassert_mem_equal(reply, &mem[12], 16);

	/* Reading the last page wraps round to page 0. */
	zassert_equal(t2t_handle(mem, len, (uint8_t[]){0x30, last_page}, 2, reply), T2T_REPLY);
	zassert_mem_equal(reply, &mem[len - 4], 4);
	zassert_mem_equal(&reply[4], mem, 12);

	zassert_equal(t2t_handle(mem, len, (uint8_t[]){0x30, last_page + 1}, 2, reply), T2T_IGNORE);
	zassert_equal(t2t_handle(mem, len, (uint8_t[]){0x50, 0x00}, 2, reply), T2T_SLEEP);
	zassert_equal(t2t_handle(mem, len, (uint8_t[]){0xA2, 4, 1, 2, 3, 4}, 6, reply), T2T_IGNORE,
		      "writes are refused");
	zassert_equal(t2t_handle(mem, len, (uint8_t[]){0x60}, 1, reply), T2T_IGNORE);
}
