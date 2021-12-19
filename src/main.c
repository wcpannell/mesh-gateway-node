/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Nordic mesh sensor observer sample
 */
#include "model_handler.h"
#include "msg.h"
#include <bluetooth/bluetooth.h>
#include <bluetooth/mesh/dk_prov.h>
#include <bluetooth/mesh/models.h>
#include <device.h>
#include <dk_buttons_and_leds.h>
#include <drivers/hwinfo.h>
#include <drivers/uart.h>
#include <errno.h>
#include <kernel.h>
#include <settings/settings.h>
#include <sys/ring_buffer.h>
#include <usb/usb_device.h>
#include <zephyr.h>

// Define & initialize event_msgq, written to by model_handler, read from
// irq
K_MSGQ_DEFINE(event_msgq, sizeof(struct msg_pub_payload), 16, 4);

// UART buffers
RING_BUF_DECLARE(tx_buf, 256);
RING_BUF_DECLARE(rx_buf, 256);

// Interrupt Service Routine
static void interrupt_handler(const struct device *dev, void *user_data)
{
	// struct serial_data *dev_data = user_data;

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			// just printk anything received for now.
			// TODO: impl receive message handling.
			uint8_t buf[16];
			size_t read;
			read = uart_fifo_read(dev, buf, sizeof(buf));
			if (read) {
				printk("USB UART Rx %u Bytes: ", read);
				for (int i = 0; i < read; i++) {
					printk("%x", buf[i]);
				}
				printk("\n");
			}

			// Re-enable TX irq
			// Assume USB-UART can do bidirectional. Not disabling, so don't need to
			// enable
			// uart_irq_tx_enable(dev);
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t *buf;
			size_t len;
			size_t wrote = 0;

			len = ring_buf_get_claim(&tx_buf, &buf, 16); // 16 is just a nice size
			if (!len) {
				printk("USB Tx Queue Empty!\n");
				uart_irq_tx_disable(dev);
			} else {
				wrote = uart_fifo_fill(dev, buf, len);
				printk("USB UART Tx sent %u Bytes\n", wrote);
			}
			ring_buf_get_finish(&tx_buf, wrote);
		}
	}
}

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	dk_leds_init();
	dk_buttons_init(NULL);

	err = bt_mesh_init(bt_mesh_dk_prov_init(), model_handler_init());
	if (err) {
		printk("Initializing mesh failed (err %d)\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	printk("Mesh already provisioned? %d\n", bt_mesh_is_provisioned());

	/* This will be a no-op if settings_load() loaded provisioning info */
	printk("Retval of bt_mesh_prov_enable: %d\n",
	       bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT));

	printk("Mesh initialized\n");
}

struct device *usb_uart_dev;

struct k_work_delayable uart_handler_work;
void uart_handler(struct k_work *work)
{
	// "idle" loop
	// TODO: implement a means to receive a NACK
	// TODO: implement a means to resend if receive a NACK
	size_t serbuf_wrote;
	struct message event_message = {
		.type = MSG_PUB,
		.payload_size = MSG_PUB_PAYLOAD_SIZE,
		.payload = NULL,
	};
	struct msg_pub_payload payload;
	uint8_t msg_buf[MSG_PUB_BUF_SIZE];
	uint32_t dtr;

	// Make sure there's something on the line before trying to Tx
	// USB CDC ACM is not happy unless there's someone on the line.
	uart_line_ctrl_get(usb_uart_dev, UART_LINE_CTRL_DTR, &dtr);
	if (dtr) {
		// send event
		if (k_msgq_num_used_get(&event_msgq)) {
			k_msgq_get(&event_msgq, &payload, K_MSEC(100));
			printk("Got Payload from %x: %x = %u\n", payload.addr, payload.property_id,
			       payload.value);
			event_message.payload = &payload;

			serbuf_wrote = msg_serialize(&event_message, msg_buf, MSG_PUB_BUF_SIZE);
			if (serbuf_wrote != MSG_PUB_BUF_SIZE) {
				printk("Error Serializing Payload. Wrote %u bytes\n", serbuf_wrote);
				for (int i = 0; i < serbuf_wrote; i++) {
					printk("%X", msg_buf[i]);
				}
				printk("\n");
			}

			// push message to tx_buf
			serbuf_wrote = ring_buf_put(&tx_buf, msg_buf, MSG_PUB_BUF_SIZE);
			if (serbuf_wrote != MSG_PUB_BUF_SIZE) {
				printk("Error Pushing into tx_buf! considering handling this!\n");
			}

			// enable UART Tx IRQ, if was disabled, we have data to spit.
			uart_irq_tx_enable(usb_uart_dev);
		}
	} else {
		uart_irq_tx_disable(usb_uart_dev);
	}
	k_work_schedule(&uart_handler_work, K_MSEC(100));
}

void main(void)
{
	int err;

	printk("Initializing USB UART...\n");

	usb_uart_dev = device_get_binding("CDC_ACM_0");
	if (!usb_uart_dev) {
		printk("USB UART device CDC_ACM_0 not found!\n");
		printk("bailing out...\n");
		return;
	}

	if (usb_enable(NULL) != 0) {
		printk("Failed to enable USB\n");
		printk("bailing out...\n");
		return;
	}

	uart_irq_callback_set(usb_uart_dev, interrupt_handler);
	uart_irq_rx_enable(usb_uart_dev);
	k_work_init_delayable(&uart_handler_work, uart_handler);
	k_work_schedule(&uart_handler_work, K_NO_WAIT);

	printk("Initializing Bluetooth...\n");

	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}
}
