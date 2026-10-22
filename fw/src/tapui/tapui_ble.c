#include "tapui.h"

#if defined(CONFIG_TAPUI_NFC)
#include "nfc_tag.h"
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tapui, LOG_LEVEL_INF);

#define TAPUI_UUID(n) BT_UUID_128_ENCODE(0xc7170000 + (n), 0x8a2d, 0x43c1, 0xa783, 0x922a8834a1fe)

static const struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(TAPUI_UUID(1));
static const struct bt_uuid_128 rx_uuid = BT_UUID_INIT_128(TAPUI_UUID(2));
static const struct bt_uuid_128 tx_uuid = BT_UUID_INIT_128(TAPUI_UUID(3));

static const struct tapui_handlers *handlers;
static struct bt_conn *current;
static char name[16];
static char url[sizeof(CONFIG_TAPUI_URL_BASE) + 8 + sizeof(name)];

/* Commands are handled on this thread, not the Bluetooth RX thread: a
 * handler that sends blocks until the stack has buffers, and blocking the RX
 * thread for that would deadlock it. */
K_MSGQ_DEFINE(lines, TAPUI_LINE_MAX, 4, 1);
static struct tapui_linebuf linebuf;

static K_MUTEX_DEFINE(tx_lock);
static char tx_line[CONFIG_TAPUI_TX_LINE_MAX];

static void queue_line(const char *line, void *ctx)
{
	ARG_UNUSED(ctx);
	char copy[TAPUI_LINE_MAX] = "";

	/* An empty line stands for one that was too long. */
	if (line) {
		strncpy(copy, line, sizeof(copy) - 1);
		if (copy[0] == '\0') {
			return;
		}
	}
	if (k_msgq_put(&lines, copy, K_NO_WAIT) != 0) {
		LOG_WRN("command dropped: queue full");
	}
}

static ssize_t rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(offset);
	ARG_UNUSED(flags);

	tapui_linebuf_feed(&linebuf, buf, len, queue_line, NULL);
	return len;
}

BT_GATT_SERVICE_DEFINE(tapui_svc,
	BT_GATT_PRIMARY_SERVICE(&svc_uuid),
	BT_GATT_CHARACTERISTIC(&rx_uuid.uuid, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, rx_write, NULL),
	BT_GATT_CHARACTERISTIC(&tx_uuid.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL,
			       NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

#define TX_ATTR (&tapui_svc.attrs[3])

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, TAPUI_UUID(1)),
};

static struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, name, 0),
};

static void advertise(struct k_work *work)
{
	ARG_UNUSED(work);
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err && err != -EALREADY) {
		LOG_ERR("advertising failed: %d", err);
	}
}

static K_WORK_DEFINE(advertise_work, advertise);

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		return;
	}
	current = bt_conn_ref(conn);
	linebuf.len = 0;
	linebuf.overflow = false;
	k_msgq_purge(&lines);
	LOG_INF("connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (conn == current) {
		bt_conn_unref(current);
		current = NULL;
	}
	LOG_INF("disconnected (0x%02x)", reason);
}

/* The connection's resources are free again only here, so advertising
 * restarts here rather than in disconnected(). */
static void recycled(void)
{
	k_work_submit(&advertise_work);
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
};

static int notify_all(struct bt_conn *conn, const uint8_t *data, size_t len)
{
	size_t chunk = bt_gatt_get_mtu(conn) - 3;

	while (len > 0) {
		size_t n = MIN(len, chunk);
		int err;

		/* Out of buffers means the link is behind, not broken: wait. */
		for (int tries = 0;; tries++) {
			err = bt_gatt_notify(conn, TX_ATTR, data, n);
			if (err != -ENOMEM || tries == 200) {
				break;
			}
			k_sleep(K_MSEC(5));
		}
		if (err) {
			return err;
		}
		data += n;
		len -= n;
	}
	return 0;
}

int tapui_send(const char *fmt, ...)
{
	struct bt_conn *conn = current;
	va_list ap;
	int n;
	int err;

	if (!conn || !bt_gatt_is_subscribed(conn, TX_ATTR, BT_GATT_CCC_NOTIFY)) {
		return -ENOTCONN;
	}

	k_mutex_lock(&tx_lock, K_FOREVER);
	va_start(ap, fmt);
	n = vsnprintf(tx_line, sizeof(tx_line) - 1, fmt, ap);
	va_end(ap);
	if (n < 0 || n >= (int)sizeof(tx_line) - 1) {
		k_mutex_unlock(&tx_lock);
		LOG_ERR("line over CONFIG_TAPUI_TX_LINE_MAX");
		return -ENOSPC;
	}
	tx_line[n++] = '\n';
	err = notify_all(conn, (const uint8_t *)tx_line, n);
	k_mutex_unlock(&tx_lock);
	return err;
}

static int send_state(const char *id, const char *json_value)
{
	char qid[48];

	tapui_json_str(qid, sizeof(qid), id);
	return tapui_send("{\"t\":\"state\",\"id\":%s,\"v\":%s}", qid, json_value);
}

int tapui_send_bool(const char *id, bool v)
{
	return send_state(id, v ? "true" : "false");
}

int tapui_send_int(const char *id, int v)
{
	char num[12];

	snprintf(num, sizeof(num), "%d", v);
	return send_state(id, num);
}

int tapui_send_str(const char *id, const char *v)
{
	char q[96];

	if (tapui_json_str(q, sizeof(q), v) >= sizeof(q)) {
		return -ENOSPC;
	}
	return send_state(id, q);
}

int tapui_send_err(const char *msg)
{
	char q[96];

	tapui_json_str(q, sizeof(q), msg);
	return tapui_send("{\"t\":\"err\",\"msg\":%s}", q);
}

bool tapui_connected(void)
{
	return current != NULL;
}

const char *tapui_name(void)
{
	return name;
}

const char *tapui_url(void)
{
	return url;
}

static void command_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	char line[TAPUI_LINE_MAX];
	struct tapui_cmd cmd;

	for (;;) {
		k_msgq_get(&lines, line, K_FOREVER);
		if (line[0] == '\0') {
			tapui_send_err("command too long");
			continue;
		}
		if (tapui_cmd_parse(line, &cmd) != 0) {
			tapui_send_err("not a command");
			continue;
		}
		if (cmd.op == TAPUI_OP_HELLO) {
			handlers->on_hello();
		} else {
			handlers->on_cmd(&cmd);
		}
	}
}

K_THREAD_DEFINE(tapui_cmd_tid, CONFIG_TAPUI_THREAD_STACK_SIZE, command_thread, NULL, NULL, NULL,
		K_PRIO_PREEMPT(7), 0, K_TICKS_FOREVER);

int tapui_start(const struct tapui_handlers *h)
{
	bt_addr_le_t addr;
	size_t count = 1;
	int err;

	handlers = h;

	err = bt_enable(NULL);
	if (err) {
		return err;
	}

	bt_id_get(&addr, &count);
	snprintf(name, sizeof(name), "TapUI-%02X%02X", addr.a.val[1], addr.a.val[0]);
	bt_set_name(name);
	sd[0].data_len = strlen(name);
	snprintf(url, sizeof(url), "%s?n=%s", CONFIG_TAPUI_URL_BASE, name);

	k_thread_start(tapui_cmd_tid);
	advertise(NULL);
	LOG_INF("advertising as %s", name);
	LOG_INF("link: %s", url);

#if defined(CONFIG_TAPUI_NFC)
	err = nfc_tag_start(url);
	if (err) {
		LOG_ERR("NFC tag failed to start: %d", err);
	}
#endif
	return 0;
}
