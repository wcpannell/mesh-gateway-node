#ifndef _MSG_H
#define _MSG_H

#include <stddef.h>
#include <stdint.h>

// CRC8 polynomial is x8 + x5 + x4 + 1 = 0b0011_0001
#define MSG_CRC_POLY 0x31

// CRC8 IV is 0xff
#define MSG_CRC_IV 0xff

// Publish Event msg buffer is always 10 bytes
#define MSG_PUB_BUF_SIZE 10
#define MSG_PUB_PAYLOAD_SIZE 6

// Minimum size of packet is 4 bytes for 0-sized payload
#define MSG_BUF_MIN_SIZE 4

enum msg_errors {
  MSG_NO_ERROR = 0,
  MSG_ERR_STARTBYTE,
  MSG_ERR_CRC,
  MSG_ERR_LEN,
  MSG_ERR_NOIMPL,
};

enum msg_type {
  MSG_POLL = 0,    // NULL payload
  MSG_BACKLOG = 1, // uint8_t payload
  MSG_PUB = 1,     // struct msg_pub_payload
  MSG_ACK = 0xFE,  // NULL payload
  MSG_NACK = 0xFF, // NULL payload
};

struct message {
  enum msg_type type;
  size_t payload_size; // 255 max
  union {
    struct msg_pub_payload *payload;
    uint8_t value;
  };
};

struct msg_pub_payload {
  uint16_t addr;
  uint16_t property_id;
  uint16_t value; // Value is scaled 0.01, Temp or Humidity
};

/**
 * @brief Serialize a message
 *
 * Flattens the data structure and stuffs it into the byte buffer
 *
 * @param[in] msg message data structure
 * @param[out] buf existing byte buffer of sufficient length.
 * @param[in] len (maximum) length of the provided buffer.
 *
 * @returns number of bytes written into buf
 *
 * Serialized Message format:
 * byte: desc
 * 0: Start byte, always 0xAA for valid packets.
 * 1: Message Type, see enum msg_type.
 * 2: Payload Size, number of bytes in the payload.
 * 3 - (3+N-1): payload, Payload Size bytes long, interpretation varies
 *            by message type
 * 3+N: CRC8, uses the MSG_CRC_POLY polynomial and MSG_CRC_IV initial value
 */
size_t msg_serialize(const struct message *msg, uint8_t *buf, const size_t len);

/**
 * @brief Deserialize a message
 *
 * @param[in] buf byte buffer containing flat message data
 * @param[in] len length of buffer
 * @param[out] msg message data structure to be filled with buf data.
 *
 * @returns 0 on success
 */
int msg_deserialize(uint8_t *buf, size_t len, struct message *msg);

#endif
