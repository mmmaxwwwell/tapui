/* NFC Forum Type 2 Tag on the nRF52's NFCT peripheral, over nrfx directly:
 * upstream Zephyr has no NFC tag library (Nordic's lives in NCS). The
 * hardware does anticollision and CRC; this answers READ and HLTA. */
#include "nfc_tag.h"
#include "ndef.h"

#include <errno.h>
#include <nrfx_nfct.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/onoff.h>

LOG_MODULE_REGISTER(nfc_tag, LOG_LEVEL_INF);

#define TAG_MEM_MAX 256

/* EasyDMA reads and writes these, so they live in RAM. */
static uint8_t tag_mem[TAG_MEM_MAX];
static size_t tag_len;
static uint8_t rx_buf[16];
static uint8_t tx_buf[T2T_READ_LEN];

static const struct device *const hfclk = DEVICE_DT_GET(DT_NODELABEL(hfclk));
static struct onoff_client hfclk_cli;
static bool hfclk_requested;

static void listen(void)
{
	nrfx_nfct_data_desc_t rx = {.p_data = rx_buf, .data_size = sizeof(rx_buf)};

	nrfx_nfct_rx(&rx);
}

static void hfclk_ready(struct onoff_manager *mgr, struct onoff_client *cli, uint32_t state,
			int res)
{
	ARG_UNUSED(mgr);
	ARG_UNUSED(cli);
	ARG_UNUSED(state);

	/* NFCT may only be activated with the crystal running. */
	if (res >= 0) {
		nrfx_nfct_state_force(NRFX_NFCT_STATE_ACTIVATED);
	}
}

static void on_frame(const nrfx_nfct_evt_rx_frameend_t *rx)
{
	if (rx->rx_status != 0) {
		listen();
		return;
	}

	switch (t2t_handle(tag_mem, tag_len, rx->rx_data.p_data, rx->rx_data.data_size, tx_buf)) {
	case T2T_REPLY: {
		nrfx_nfct_data_desc_t tx = {.p_data = tx_buf, .data_size = sizeof(tx_buf)};

		nrfx_nfct_tx(&tx, NRF_NFCT_FRAME_DELAY_MODE_WINDOWGRID);
		break;
	}
	case T2T_SLEEP:
		nrfx_nfct_init_substate_force(NRFX_NFCT_ACTIVE_STATE_SLEEP);
		break;
	case T2T_IGNORE:
		listen();
		break;
	}
}

static void nfct_event(const nrfx_nfct_evt_t *evt)
{
	switch (evt->evt_id) {
	case NRFX_NFCT_EVT_FIELD_DETECTED:
		if (!hfclk_requested) {
			hfclk_requested = true;
			sys_notify_init_callback(&hfclk_cli.notify, hfclk_ready);
			nrf_clock_control_request(hfclk, NULL, &hfclk_cli);
		}
		break;
	case NRFX_NFCT_EVT_FIELD_LOST:
		if (hfclk_requested) {
			hfclk_requested = false;
			nrf_clock_control_cancel_or_release(hfclk, NULL, &hfclk_cli);
		}
		break;
	case NRFX_NFCT_EVT_SELECTED:
		listen();
		break;
	case NRFX_NFCT_EVT_RX_FRAMEEND:
		on_frame(&evt->params.rx_frameend);
		break;
	case NRFX_NFCT_EVT_TX_FRAMEEND:
		listen();
		break;
	case NRFX_NFCT_EVT_ERROR:
		/* Missed the reply window; the reader will retry. */
		listen();
		break;
	default:
		break;
	}
}

int nfc_tag_start(const char *uri)
{
	uint8_t ndef[TAG_MEM_MAX];
	uint8_t uid[T2T_UID_LEN];
	int ndef_len;
	int n;

	ndef_len = ndef_uri_message(uri, ndef, sizeof(ndef));
	if (ndef_len < 0) {
		return ndef_len;
	}

	/* The factory-unique NFCID1 from FICR. */
	n = nrfx_nfct_nfcid1_default_bytes_get(uid, sizeof(uid));
	if (n != 0) {
		return n;
	}

	n = t2t_build(uid, ndef, (size_t)ndef_len, tag_mem, sizeof(tag_mem));
	if (n < 0) {
		return n;
	}
	tag_len = (size_t)n;

	if ((NRF_UICR->NFCPINS & UICR_NFCPINS_PROTECT_Msk) == 0) {
		LOG_WRN("UICR NFCPINS has the NFC pads as GPIO; the tag can't work until UICR is "
			"erased");
	}

	IRQ_CONNECT(NFCT_IRQn, 1, nrfx_nfct_irq_handler, NULL, 0);
	IRQ_CONNECT(NRFX_CONCAT_3(TIMER, NRFX_NFCT_CONFIG_TIMER_INSTANCE_ID, _IRQn), 1,
		    nrfx_nfct_workaround_timer_handler, NULL, 0);

	nrfx_nfct_config_t cfg = {
		.rxtx_int_mask = NRFX_NFCT_EVT_FIELD_DETECTED | NRFX_NFCT_EVT_FIELD_LOST |
				 NRFX_NFCT_EVT_SELECTED | NRFX_NFCT_EVT_RX_FRAMEEND |
				 NRFX_NFCT_EVT_TX_FRAMEEND | NRFX_NFCT_EVT_ERROR,
		.cb = nfct_event,
		.irq_priority = 1,
	};

	n = nrfx_nfct_init(&cfg);
	if (n != 0) {
		return n;
	}

	nrfx_nfct_param_t id = {
		.id = NRFX_NFCT_PARAM_ID_NFCID1,
		.data.nfcid1 = {.p_id = uid, .id_size = sizeof(uid)},
	};
	nrfx_nfct_param_t sel_res = {
		.id = NRFX_NFCT_PARAM_ID_SEL_RES,
		.data.sel_res_protocol = 0, /* Type 2 Tag: not ISO-DEP */
	};

	nrfx_nfct_parameter_set(&id);
	nrfx_nfct_parameter_set(&sel_res);
	nrfx_nfct_autocolres_enable();
	nrfx_nfct_enable();

	LOG_INF("NFC tag up, %u bytes: %s", (unsigned)tag_len, uri);
	return 0;
}
