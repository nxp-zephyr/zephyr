/** @file
 *  @brief Bluetooth RFCOMM handling
 */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_BLUETOOTH_RFCOMM_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_RFCOMM_H_

/**
 * @brief RFCOMM
 * @defgroup bt_rfcomm RFCOMM
 * @ingroup bluetooth
 * @{
 */

#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/conn.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RFCOMM channels (1-30): pre-allocated for profiles to avoid conflicts */
enum {
	BT_RFCOMM_CHAN_HFP_HF = 1,
	BT_RFCOMM_CHAN_HFP_AG,
	BT_RFCOMM_CHAN_HSP_AG,
	BT_RFCOMM_CHAN_HSP_HS,
	BT_RFCOMM_CHAN_SPP,
};

struct bt_rfcomm_dlc;

/** @brief RFCOMM RPN settings structure. */
struct bt_rfcomm_port_settings {
	uint8_t  baud_rate;
	uint8_t  line_settings;
	uint8_t  flow_control;
	uint8_t  xon_char;
	uint8_t  xoff_char;
	uint16_t param_mask;
} __packed;

/** @brief RFCOMM DLC operations structure. */
struct bt_rfcomm_dlc_ops {
	/** DLC connected callback
	 *
	 *  If this callback is provided it will be called whenever the
	 *  connection completes.
	 *
	 *  @param dlc The dlc that has been connected
	 */
	void (*connected)(struct bt_rfcomm_dlc *dlc);

	/** DLC disconnected callback
	 *
	 *  If this callback is provided it will be called whenever the
	 *  dlc is disconnected, including when a connection gets
	 *  rejected or cancelled (both incoming and outgoing)
	 *
	 *  @param dlc The dlc that has been Disconnected
	 */
	void (*disconnected)(struct bt_rfcomm_dlc *dlc);

	/** DLC recv callback
	 *
	 *  @param dlc The dlc receiving data.
	 *  @param buf Buffer containing incoming data.
	 */
	void (*recv)(struct bt_rfcomm_dlc *dlc, struct net_buf *buf);

	/** DLC sent callback
	 *
	 *  @param dlc The dlc which has sent data.
	 *  @param buf Buffer containing data has been sent.
	 *  @param err Sent result.
	 */
	void (*sent)(struct bt_rfcomm_dlc *dlc, struct net_buf *buf, int err);

	/** DLC port negotiating callback
	 *
	 *  The DLC port negotiating callback will be called whenever a
	 *  valid RPN request received.
	 *  When the callback triggered, it means the remote expects
	 *  to change the port settings of local device.
	 *
	 *  The RPN command of request is kept in parameter port_settings.
	 *  The parameter mask of port_settings indicates which parameters
	 *  are changed by remote device.
	 *  Before the callback returns, the parameter mask bit of
	 *  port_settings needs to be set by upper layer if the changing
	 *  parameter can be accepted. Or, clear the corresponding
	 *  parameter mask bit.
	 *
	 *  @param dlc The dlc receiving RPN request.
	 *  @param port_settings The RPN setting needs to be changed.
	 */
	void (*rpn)(struct bt_rfcomm_dlc *dlc, struct bt_rfcomm_port_settings *port_settings);

	/** DLC port updated callback
	 *
	 *  The port updated callback will be called whenever a
	 *  valid rpn response of getting/setting command received.
	 *
	 *  The latest port settings of peer device is kept in parameter
	 *  port_settings.
	 *
	 *  @param dlc The dlc receiving RPN request/response.
	 *  @param port_settings The latest port settings.
	 */
	void (*remote_port_updated)(struct bt_rfcomm_dlc *dlc, struct bt_rfcomm_port_settings *port_settings);
};

/** @brief Role of RFCOMM session and dlc. Used only by internal APIs
 */
typedef enum bt_rfcomm_role {
	BT_RFCOMM_ROLE_ACCEPTOR,
	BT_RFCOMM_ROLE_INITIATOR
} __packed bt_rfcomm_role_t;

/** @brief RFCOMM RPN Baud Rate. */
#define BT_RFCOMM_RPN_BAUD_RATE_2400 0x00
#define BT_RFCOMM_RPN_BAUD_RATE_4800 0x01
#define BT_RFCOMM_RPN_BAUD_RATE_7200 0x02
#define BT_RFCOMM_RPN_BAUD_RATE_9600 0x03
#define BT_RFCOMM_RPN_BAUD_RATE_19200 0x04
#define BT_RFCOMM_RPN_BAUD_RATE_38400 0x05
#define BT_RFCOMM_RPN_BAUD_RATE_57600 0x06
#define BT_RFCOMM_RPN_BAUD_RATE_115200 0x07
#define BT_RFCOMM_RPN_BAUD_RATE_230400 0x08
#define BT_RFCOMM_RPN_BAUD_RATE_DEFAULT BT_RFCOMM_RPN_BAUD_RATE_9600

/** @brief RFCOMM RPN Data Bits. */
#define BT_RFCOMM_RPN_DATA_BITS_5 0x00
#define BT_RFCOMM_RPN_DATA_BITS_6 0x01
#define BT_RFCOMM_RPN_DATA_BITS_7 0x02
#define BT_RFCOMM_RPN_DATA_BITS_8 0x03
#define BT_RFCOMM_RPN_DATA_BITS_DEFAULT BT_RFCOMM_RPN_DATA_BITS_8

/** @brief RFCOMM RPN Stop Bits. */
#define BT_RFCOMM_RPN_STOP_BITS_1   0x00
#define BT_RFCOMM_RPN_STOP_BITS_1_5 0x01
#define BT_RFCOMM_RPN_STOP_BITS_DEFAULT BT_RFCOMM_RPN_STOP_BITS_1

/** @brief RFCOMM RPN Parity. */
#define BT_RFCOMM_RPN_PARITY_NONE  0x00
#define BT_RFCOMM_RPN_PARITY_ODD   0x01
#define BT_RFCOMM_RPN_PARITY_EVEN  0x03
#define BT_RFCOMM_RPN_PARITY_MARK  0x05
#define BT_RFCOMM_RPN_PARITY_SPACE 0x07
#define BT_RFCOMM_RPN_PARITY_DEFAULT BT_RFCOMM_RPN_PARITY_NONE

/** @brief RFCOMM RPN set line settings. */
#define BT_RFCOMM_RPN_SET_LINE_SETTINGS(_data_bits, _stop_bits, _parity) \
							 (((_data_bits) & 0x3) | \
							 (((_stop_bits) & 0x1) << 2) | \
							 (((_parity) & 0x7) << 3))

/** @brief RFCOMM RPN update data bits.
 *
 *  @param old It is old value of line settings.
 *  @param new It is new value of line settings.
 */
#define BT_RFCOMM_RPN_UPDATE_DATA_BITS(old, new) \
							 (((old) & ~(0x03)) | \
							 ((new) & 0x03))

/** @brief RFCOMM RPN update stop bits.
 *
 *  @param old It is old value of line settings.
 *  @param new It is new value of line settings.
 */
#define BT_RFCOMM_RPN_UPDATE_STOP_BITS(old, new) \
							 (((old) & ~(0x04)) | \
							 ((new) & 0x04))

/** @brief RFCOMM RPN update parity.
 *
 *  @param old It is old value of line settings.
 *  @param new It is new value of line settings.
 */
#define BT_RFCOMM_RPN_UPDATE_PARITY(old, new) \
							 (((old) & ~(0x08)) | \
							 ((new) & 0x08))

/** @brief RFCOMM RPN update parity type.
 *
 *  @param old It is old value of line settings.
 *  @param new It is new value of line settings.
 */
#define BT_RFCOMM_RPN_UPDATE_PARITY_TYPE(old, new) \
							 (((old) & ~(0x30)) | \
							 ((new) & 0x30))

/** @brief RFCOMM RPN Get Data Bits from line settings. */
#define BT_RFCOMM_RPN_DATA_BITS(line_settings) ((line_settings) & 0x03)

/** @brief RFCOMM RPN Get Stop Bits from line settings. */
#define BT_RFCOMM_RPN_STOP_BITS(line_settings) (((line_settings) >> 2) & 0x01)

/** @brief RFCOMM RPN Get Parity from line settings. */
#define BT_RFCOMM_RPN_PARITY(line_settings) (((line_settings) >> 3) & 0x07)

/** @brief RFCOMM RPN Flow Control. */
#define BT_RFCOMM_RPN_FC_XON_XOFF_INPUT BIT(0)
#define BT_RFCOMM_RPN_FC_XON_XOFF_OUTPUT BIT(1)
#define BT_RFCOMM_RPN_FC_RTR_INPUT BIT(2)
#define BT_RFCOMM_RPN_FC_RTR_OUTPUT BIT(3)
#define BT_RFCOMM_RPN_FC_RTC_INPUT BIT(4)
#define BT_RFCOMM_RPN_FC_RTC_OUTPUT BIT(5)
#define BT_RFCOMM_RPN_FC_DEFAULT 0x00

/** @brief RFCOMM RPN XON Character. */
#define BT_RFCOMM_RPN_XON_CHAR_DEFAULT 0x11

/** @brief RFCOMM RPN XOFF Character. */
#define BT_RFCOMM_RPN_XOFF_CHAR_DEFAULT 0x13

/** @brief RFCOMM RPN Parameter Mask */
#define BT_RFCOMM_RPN_PARAM_MASK_BIT_RATE       BIT(0)
#define BT_RFCOMM_RPN_PARAM_MASK_DATA_BITS      BIT(1)
#define BT_RFCOMM_RPN_PARAM_MASK_STOP_BITS      BIT(2)
#define BT_RFCOMM_RPN_PARAM_MASK_PARITY         BIT(3)
#define BT_RFCOMM_RPN_PARAM_MASK_PARITY_TYPE    BIT(4)
#define BT_RFCOMM_RPN_PARAM_MASK_XON_CHAR       BIT(5)
#define BT_RFCOMM_RPN_PARAM_MASK_XOFF_CHAR      BIT(6)
#define BT_RFCOMM_RPN_PARAM_MASK_XON_XOFF_INPUT BIT(8)
#define BT_RFCOMM_RPN_PARAM_MASK_XON_XOFF_OUTPUT BIT(9)
#define BT_RFCOMM_RPN_PARAM_MASK_RTR_INPUT      BIT(10)
#define BT_RFCOMM_RPN_PARAM_MASK_RTR_OUTPUT     BIT(11)
#define BT_RFCOMM_RPN_PARAM_MASK_RTC_INPUT      BIT(12)
#define BT_RFCOMM_RPN_PARAM_MASK_RTC_OUTPUT     BIT(13)

/* Set 1 to all the param mask except reserved */
#define BT_RFCOMM_RPN_PARAM_MASK_ALL ( \
		BT_RFCOMM_RPN_PARAM_MASK_BIT_RATE        | \
		BT_RFCOMM_RPN_PARAM_MASK_DATA_BITS       | \
		BT_RFCOMM_RPN_PARAM_MASK_STOP_BITS       | \
		BT_RFCOMM_RPN_PARAM_MASK_PARITY          | \
		BT_RFCOMM_RPN_PARAM_MASK_PARITY_TYPE     | \
		BT_RFCOMM_RPN_PARAM_MASK_XON_CHAR        | \
		BT_RFCOMM_RPN_PARAM_MASK_XOFF_CHAR       | \
		BT_RFCOMM_RPN_PARAM_MASK_XON_XOFF_INPUT  | \
		BT_RFCOMM_RPN_PARAM_MASK_XON_XOFF_OUTPUT | \
		BT_RFCOMM_RPN_PARAM_MASK_RTR_INPUT       | \
		BT_RFCOMM_RPN_PARAM_MASK_RTR_OUTPUT      | \
		BT_RFCOMM_RPN_PARAM_MASK_RTC_INPUT       | \
		BT_RFCOMM_RPN_PARAM_MASK_RTC_OUTPUT        \
)

/* Full parameter mask for FC */
#define BT_RFCOMM_RPN_PARAM_MASK_FC ( \
		BT_RFCOMM_RPN_PARAM_MASK_XON_XOFF_INPUT  | \
		BT_RFCOMM_RPN_PARAM_MASK_XON_XOFF_OUTPUT | \
		BT_RFCOMM_RPN_PARAM_MASK_RTR_INPUT       | \
		BT_RFCOMM_RPN_PARAM_MASK_RTR_OUTPUT      | \
		BT_RFCOMM_RPN_PARAM_MASK_RTC_INPUT       | \
		BT_RFCOMM_RPN_PARAM_MASK_RTC_OUTPUT        \
)

/**
 * Helper to initialize RFCOMM RPN settings parameters inline
 *
 * @param _baud_rate Baud rate
 * @param _data_bits Data bits
 * @param _stop_bits Stop bits
 * @param _parity    Parity
 * @param _fc        Flow control
 * @param _xon       XON charater
 * @param _xoff      XOFF charater
 * @param _mask      Mask of RFCOMM RPN settings parameters
 */
#define BT_RFCOMM_RPN_SETTINGS_PARAM_INIT(_baud_rate, _data_bits, _stop_bits, _parity, _fc, _xon, _xoff, _mask) \
{ \
	.baud_rate = (_baud_rate), \
	.line_settings = BT_RFCOMM_RPN_SET_LINE_SETTINGS(_data_bits, _stop_bits, _parity), \
	.flow_control = (_fc), \
	.xon_char = (_xon), \
	.xoff_char = (_xoff), \
	.param_mask = (_mask), \
}

#define BT_RFCOMM_RPN_SETTINGS_PARAM_DEFAULT BT_RFCOMM_RPN_SETTINGS_PARAM_INIT( \
											 BT_RFCOMM_RPN_BAUD_RATE_DEFAULT, \
											 BT_RFCOMM_RPN_DATA_BITS_DEFAULT, \
											 BT_RFCOMM_RPN_STOP_BITS_DEFAULT, \
											 BT_RFCOMM_RPN_PARITY_DEFAULT,    \
											 BT_RFCOMM_RPN_FC_DEFAULT,        \
											 BT_RFCOMM_RPN_XON_CHAR_DEFAULT,  \
											 BT_RFCOMM_RPN_XOFF_CHAR_DEFAULT, \
											 BT_RFCOMM_RPN_PARAM_MASK_ALL     \
											 )

/* bt_rfcomm_dlc flags: */
enum {
	BT_RFCOMM_DLC_CTL_CMD,             /* Sending multiplexer control command */
	BT_RFCOMM_DLC_RPN_SET,             /* Sending set RPN command */
	BT_RFCOMM_DLC_RPN_GET,             /* Sending get RPN command */

	/* Total number of flags - must be at the end of the enum */
	BT_RFCOMM_DLC_NUM_FLAGS,
};

/** @brief RFCOMM DLC structure. */
struct bt_rfcomm_dlc {
	/* Response Timeout eXpired (RTX) timer */
	struct k_work_delayable    rtx_work;

	/* Queue for outgoing data */
	struct k_fifo              tx_queue;

	/* TX credits, Reuse as a binary sem for MSC FC if CFC is not enabled */
	struct k_sem               tx_credits;

	struct bt_rfcomm_session  *session;
	struct bt_rfcomm_dlc_ops  *ops;
	struct bt_rfcomm_dlc      *_next;

	bt_security_t              required_sec_level;
	bt_rfcomm_role_t           role;

	uint16_t                      mtu;
	uint8_t                       dlci;
	uint8_t                       state;
	uint8_t                       rx_credit;

	ATOMIC_DEFINE(flags, BT_RFCOMM_DLC_NUM_FLAGS);

	/* Remote port settings in use */
	struct bt_rfcomm_port_settings port_settings;

	/* Local port settings in use */
	struct bt_rfcomm_port_settings local_port_settings;

	/* Stack & kernel data for TX thread */
	struct k_thread            tx_thread;
	K_KERNEL_STACK_MEMBER(stack, 256);
};

struct bt_rfcomm_server {
	/** Server Channel */
	uint8_t channel;

	/** Server accept callback
	 *
	 *  This callback is called whenever a new incoming connection requires
	 *  authorization.
	 *
	 *  @param conn The connection that is requesting authorization
	 *  @param dlc Pointer to received the allocated dlc
	 *
	 *  @return 0 in case of success or negative value in case of error.
	 */
	int (*accept)(struct bt_conn *conn, struct bt_rfcomm_dlc **dlc);

	struct bt_rfcomm_server	*_next;
};

/** @brief Register RFCOMM server
 *
 *  Register RFCOMM server for a channel, each new connection is authorized
 *  using the accept() callback which in case of success shall allocate the dlc
 *  structure to be used by the new connection.
 *
 *  @param server Server structure.
 *
 *  @return 0 in case of success or negative value in case of error.
 */
int bt_rfcomm_server_register(struct bt_rfcomm_server *server);

/** @brief Connect RFCOMM channel
 *
 *  Connect RFCOMM dlc by channel, once the connection is completed dlc
 *  connected() callback will be called. If the connection is rejected
 *  disconnected() callback is called instead.
 *
 *  @param conn Connection object.
 *  @param dlc Dlc object.
 *  @param channel Server channel to connect to.
 *
 *  @return 0 in case of success or negative value in case of error.
 */
int bt_rfcomm_dlc_connect(struct bt_conn *conn, struct bt_rfcomm_dlc *dlc,
			  uint8_t channel);

/** @brief Send data to RFCOMM
 *
 *  Send data from buffer to the dlc. Length should be less than or equal to
 *  mtu.
 *
 *  @param dlc Dlc object.
 *  @param buf Data buffer.
 *
 *  @return Bytes sent in case of success or negative value in case of error.
 */
int bt_rfcomm_dlc_send(struct bt_rfcomm_dlc *dlc, struct net_buf *buf);

/** @brief Disconnect RFCOMM dlc
 *
 *  Disconnect RFCOMM dlc, if the connection is pending it will be
 *  canceled and as a result the dlc disconnected() callback is called.
 *
 *  @param dlc Dlc object.
 *
 *  @return 0 in case of success or negative value in case of error.
 */
int bt_rfcomm_dlc_disconnect(struct bt_rfcomm_dlc *dlc);

/** @brief Allocate the buffer from pool after reserving head room for RFCOMM,
 *  L2CAP and ACL headers.
 *
 *  @param pool Which pool to take the buffer from.
 *
 *  @return New buffer.
 */
struct net_buf *bt_rfcomm_create_pdu(struct net_buf_pool *pool);

/** @brief Start RFCOMM DLC Remote Port Negotiation (RPN)
 *
 *  Start RFCOMM DLC Remote Port Negotiation. When the response received,
 *  the callback dlc->ops->remote_port_updated will be notified.
 *
 *  If the parameter port_settings it is NULL, the function requests to
 *  get the remote port settings.
 *
 *  @param dlc Dlc object.
 *  @param port_settings Remote port settings.
 *
 *  @return 0 in case of success or negative value in case of error.
 */
int bt_rfcomm_dlc_rpn(struct bt_rfcomm_dlc *dlc, struct bt_rfcomm_port_settings *port_settings);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_RFCOMM_H_ */
