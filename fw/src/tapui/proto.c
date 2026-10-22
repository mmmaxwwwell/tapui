#include "proto.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

void tapui_linebuf_feed(struct tapui_linebuf *lb, const uint8_t *data, size_t len,
			tapui_line_cb cb, void *ctx)
{
	for (size_t i = 0; i < len; i++) {
		char c = (char)data[i];

		if (c == '\n') {
			if (lb->overflow) {
				cb(NULL, ctx);
			} else {
				if (lb->len > 0 && lb->buf[lb->len - 1] == '\r') {
					lb->len--;
				}
				lb->buf[lb->len] = '\0';
				cb(lb->buf, ctx);
			}
			lb->len = 0;
			lb->overflow = false;
		} else if (lb->len < TAPUI_LINE_MAX - 1) {
			lb->buf[lb->len++] = c;
		} else {
			lb->overflow = true;
		}
	}
}

static char *next_word(char **s)
{
	char *start = *s;
	char *sp = strchr(start, ' ');

	if (sp) {
		*sp = '\0';
		*s = sp + 1;
	} else {
		*s = start + strlen(start);
	}
	return start;
}

int tapui_cmd_parse(char *line, struct tapui_cmd *cmd)
{
	char *rest = line;
	const char *op = next_word(&rest);

	cmd->id = NULL;
	cmd->value = NULL;

	if (strcmp(op, "hello") == 0) {
		cmd->op = TAPUI_OP_HELLO;
		return *rest == '\0' ? 0 : -EINVAL;
	}

	if (strcmp(op, "press") == 0) {
		cmd->op = TAPUI_OP_PRESS;
		cmd->id = rest;
		return (*rest != '\0' && strchr(rest, ' ') == NULL) ? 0 : -EINVAL;
	}

	if (strcmp(op, "set") == 0) {
		bool has_value = strchr(rest, ' ') != NULL;

		cmd->op = TAPUI_OP_SET;
		cmd->id = next_word(&rest);
		cmd->value = rest;
		return (*cmd->id != '\0' && has_value) ? 0 : -EINVAL;
	}

	return -EINVAL;
}

size_t tapui_json_str(char *out, size_t cap, const char *s)
{
	size_t n = 0;

#define PUT(ch)                                                                                    \
	do {                                                                                       \
		if (n + 1 < cap) {                                                                 \
			out[n] = (ch);                                                             \
		}                                                                                  \
		n++;                                                                               \
	} while (0)

	PUT('"');
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\') {
			PUT('\\');
			PUT((char)c);
		} else if (c < 0x20) {
			char esc[7];

			snprintf(esc, sizeof(esc), "\\u%04x", c);
			for (const char *e = esc; *e; e++) {
				PUT(*e);
			}
		} else {
			PUT((char)c);
		}
	}
	PUT('"');
#undef PUT

	if (cap > 0) {
		out[n < cap ? n : cap - 1] = '\0';
	}
	return n;
}
