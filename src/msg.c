#include "msg.h"
#include <sys/crc.h>

/**
 * @brief get a "fresh" message from the MessageQueue
 */
struct message *msg_get_message;

/**
 * @brief return a message to the MessageQueue
 */

/**
 * @brief Serialize a struct msg_pub_payload
 *
 * @param[in] pub_payload to be serialized
 * @param[out] buf holds serialized data. Must be at least 6 bytes long.
 *
 * @returns size of flattened datastructure (fixed 6 bytes)
 *
 * Helper for msg_serialize that flattens the datastructure and stuffs it into
 * the byte buffer.
 */
static size_t pubpayload_tobuf(struct msg_pub_payload *pub_payload, uint8_t *buf)
{
	return 6;
}

size_t msg_serialize(const struct message *msg, uint8_t *buf, const size_t len)
{
	size_t crc_pos = 3; // assume 0-sized payload

	if (len < MSG_BUF_MIN_SIZE) {
		// too small to hold a message.
		return 0;
	}

	// Message Start bit
	buf[0] = 0xAA;
	buf[1] = (msg->type) & 0xff;
	buf[2] = (msg->payload_size) & 0xff;

	if (msg->type != MSG_PUB) {
		buf[3] = msg->value;
		crc_pos++;
	} else {
		buf[3] = (msg->payload->addr >> 8) & 0xff;
		buf[4] = msg->payload->addr & 0xff;
		buf[5] = (msg->payload->property_id >> 8) & 0xff;
		buf[6] = msg->payload->property_id & 0xff;
		buf[7] = (msg->payload->value >> 8) & 0xff;
		buf[8] = msg->payload->value & 0xff;
		crc_pos = 9;
	}

	buf[crc_pos] = crc8(buf, crc_pos, MSG_CRC_POLY, MSG_CRC_IV, false);
	return crc_pos + 1;
}

int msg_deserialize(uint8_t *buf, size_t len, struct message *msg)
{
	uint_fast8_t crc_pos = 3;

	if (len < MSG_BUF_MIN_SIZE) {
		// too small to hold a message.
		return MSG_ERR_LEN;
	}

	if (buf[0] != 0xAA) {
		return MSG_ERR_STARTBYTE;
	};
	msg->type = buf[1] & 0xff;
	msg->payload_size = buf[2] & 0xff;

	if (msg->type != MSG_PUB) {
		msg->value = buf[3];
	} else {
		// No need, we'll never deserialize this.
		crc_pos = 9;
		return MSG_ERR_NOIMPL; // not implemented
	}

	// Check CRC
	if (buf[crc_pos] != crc8(buf, len - 1, MSG_CRC_POLY, MSG_CRC_IV, false)) {
		return MSG_ERR_CRC; // bad CRC
	}

	return MSG_NO_ERROR;
}
