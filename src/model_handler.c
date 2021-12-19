/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "model_handler.h"
#include "msg.h"
#include <bluetooth/bluetooth.h>
#include <bluetooth/mesh/models.h>
#include <dk_buttons_and_leds.h>
#include <kernel.h>

#define GET_DATA_INTERVAL 3000

extern struct k_msgq event_msgq;

/**
 * @brief converts a received publish event into a message
 */
static void enqueue_event(const struct bt_mesh_msg_ctx *ctx,
			  const struct bt_mesh_sensor_type *sensor,
			  const struct sensor_value *value)
{
	uint16_t temp_value;
	// Just one event message needed, we'll immediately enqueue it
	struct msg_pub_payload event_payload;

	// Sender's address
	event_payload.addr = ctx->addr;

	// Sensor type
	event_payload.property_id = sensor->id;

	// Value is a "fixed position decimal"
	// e.g. 1234 = 12.34; 123456 = 1234.56
	temp_value = (uint16_t)(value->val1 * 100);
	temp_value += (uint16_t)((value->val2 / 10000) & 0xFFFF);
	event_payload.value = temp_value;

	// enqueue message
	k_msgq_put(&event_msgq, &event_payload, K_NO_WAIT);
	//printk("fake enqueue of %u = %u from %u\n", event_payload.property_id, event_payload.value,
	//       event_payload.addr);
}

static void sensor_cli_data_cb(struct bt_mesh_sensor_cli *cli, struct bt_mesh_msg_ctx *ctx,
			       const struct bt_mesh_sensor_type *sensor,
			       const struct sensor_value *value)
{
	switch (sensor->id) {
	case (0x0075): // bt_mesh_sensor_precise_present_amb_temp.id
		printk("Temperature update from %#4.4x! %s°C\n", ctx->addr,
		       bt_mesh_sensor_ch_str_real(value));
		enqueue_event(ctx, sensor, value);
		break;
	case (0x0076): // bt_mesh_sensor_present_amb_rel_humidity.id
		printk("Humidity Update from %#4.4x! %s\n", ctx->addr,
		       bt_mesh_sensor_ch_str_real(value));
		enqueue_event(ctx, sensor, value);
		break;
	default:
		printk("Unexpected. %x sensor with %u channels! %s\n", sensor->id,
		       sensor->channel_count, bt_mesh_sensor_ch_str_real(value));
	}
}

static void sensor_cli_series_entry_cb(struct bt_mesh_sensor_cli *cli, struct bt_mesh_msg_ctx *ctx,
				       const struct bt_mesh_sensor_type *sensor, uint8_t index,
				       uint8_t count,
				       const struct bt_mesh_sensor_series_entry *entry)
{
	printk("Relative runtime in %d to %d degrees: %s percent\n", entry->value[1].val1,
	       entry->value[2].val1, bt_mesh_sensor_ch_str_real(&entry->value[0]));
}

static const struct bt_mesh_sensor_cli_handlers bt_mesh_sensor_cli_handlers_1 = {
	.data = sensor_cli_data_cb,
	.series_entry = sensor_cli_series_entry_cb,
};

static struct bt_mesh_sensor_cli sensor_cli =
	BT_MESH_SENSOR_CLI_INIT(&bt_mesh_sensor_cli_handlers_1);

static struct k_work_delayable get_data_work;

// static struct bt_mesh_sensor_data sensors[2] = {
// 	{
// 		.type = &bt_mesh_sensor_precise_present_amb_temp,
// 		.value = { .val1 = 0, .val2 = 0 },
// 	},
// 	{ .type = &bt_mesh_sensor_present_amb_rel_humidity, .value = { .val1 =
// 0, .val2 = 0 } },
// };
uint32_t sensor_count = 3;

static void get_data(struct k_work *work)
{
	// int err;

	// handles read of found sensors with sensor_cli callback
	// err = bt_mesh_sensor_cli_all_get(&sensor_cli, NULL, NULL, &sensor_count);
	// printk("Found %u sensors\n", sensor_count);

	// if (err == 0) {
	// 	for (uint32_t i = 0; i < sensor_count; i++) {
	// 		printk("grabbed data of type %x, w/ val %s\n",
	// sensors[i].type->id,
	// bt_mesh_sensor_ch_str_real(sensors[i].value));
	// 	}
	// }

	k_work_schedule(&get_data_work, K_MSEC(GET_DATA_INTERVAL));
}

/* Set up a repeating delayed work to blink the DK's LEDs when attention is
 * requested.
 */
static struct k_work_delayable attention_blink_work;
static bool attention;

static void attention_blink(struct k_work *work)
{
	static int idx;
	const uint8_t pattern[] = {
		BIT(0) | BIT(1),
		BIT(1) | BIT(2),
		BIT(2) | BIT(3),
		BIT(3) | BIT(0),
	};

	if (attention) {
		dk_set_leds(pattern[idx++ % ARRAY_SIZE(pattern)]);
		k_work_reschedule(&attention_blink_work, K_MSEC(30));
	} else {
		dk_set_leds(DK_NO_LEDS_MSK);
	}
}

static void attention_on(struct bt_mesh_model *mod)
{
	attention = true;
	k_work_reschedule(&attention_blink_work, K_NO_WAIT);
}

static void attention_off(struct bt_mesh_model *mod)
{
	/* Will stop rescheduling blink timer */
	attention = false;
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
	.attn_on = attention_on,
	.attn_off = attention_off,
};

static struct bt_mesh_health_srv health_srv = {
	.cb = &health_srv_cb,
};

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(1,
		     BT_MESH_MODEL_LIST(BT_MESH_MODEL_CFG_SRV,
					BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
					BT_MESH_MODEL_SENSOR_CLI(&sensor_cli)),
		     BT_MESH_MODEL_NONE),
};

static const struct bt_mesh_comp comp = {
	.cid = CONFIG_BT_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

const struct bt_mesh_comp *model_handler_init(void)
{
	k_work_init_delayable(&attention_blink_work, attention_blink);
	k_work_init_delayable(&get_data_work, get_data);

	k_work_schedule(&get_data_work, K_MSEC(GET_DATA_INTERVAL));

	return &comp;
}
