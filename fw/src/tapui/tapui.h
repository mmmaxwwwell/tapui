/* A device's side of TapUI: advertise, accept the page's commands, send it
 * lines, and put the page's link on the NFC tag. docs/protocol.md. */
#ifndef TAPUI_H
#define TAPUI_H

#include <stdbool.h>

#include "proto.h"

struct tapui_handlers {
	/* The page asked for everything: send the ui line, then each value. */
	void (*on_hello)(void);
	/* set or press. Runs on TapUI's own thread, so it may call tapui_send. */
	void (*on_cmd)(const struct tapui_cmd *cmd);
};

int tapui_start(const struct tapui_handlers *handlers);

/* The advertised name, "TapUI-1A2B". Valid after tapui_start. */
const char *tapui_name(void);
/* The page's link with this device's name in it. Valid after tapui_start. */
const char *tapui_url(void);

bool tapui_connected(void);

/* printf one line to the page ('\n' is added). -ENOTCONN when nobody is
 * listening, -ENOSPC if it doesn't fit CONFIG_TAPUI_TX_LINE_MAX. */
int tapui_send(const char *fmt, ...);

/* {"t":"state",...} for the common value kinds. */
int tapui_send_bool(const char *id, bool v);
int tapui_send_int(const char *id, int v);
int tapui_send_str(const char *id, const char *v);
int tapui_send_err(const char *msg);

#endif
