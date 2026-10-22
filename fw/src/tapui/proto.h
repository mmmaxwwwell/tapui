/* The device side of docs/protocol.md that needs no Bluetooth: line
 * reassembly, command parsing, JSON string quoting. Tested on native_sim. */
#ifndef TAPUI_PROTO_H
#define TAPUI_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Room for a 127-byte command and its terminating NUL. */
#define TAPUI_LINE_MAX 128

struct tapui_linebuf {
	char buf[TAPUI_LINE_MAX];
	size_t len;
	bool overflow;
};

/* line is NUL-terminated without its '\n' (or '\r\n'); NULL means a line
 * was longer than TAPUI_LINE_MAX - 1 and was dropped. */
typedef void (*tapui_line_cb)(const char *line, void *ctx);

void tapui_linebuf_feed(struct tapui_linebuf *lb, const uint8_t *data, size_t len,
			tapui_line_cb cb, void *ctx);

enum tapui_op {
	TAPUI_OP_HELLO,
	TAPUI_OP_SET,
	TAPUI_OP_PRESS,
};

struct tapui_cmd {
	enum tapui_op op;
	const char *id;    /* NULL for hello */
	const char *value; /* set only; may contain spaces, may be empty */
};

/* Splits line in place; cmd points into it. -EINVAL if it isn't a command. */
int tapui_cmd_parse(char *line, struct tapui_cmd *cmd);

/* Writes s as a quoted JSON string. Returns the length it needs (excluding
 * the NUL), like snprintf; the output is truncated if that is >= cap. */
size_t tapui_json_str(char *out, size_t cap, const char *s);

#endif
