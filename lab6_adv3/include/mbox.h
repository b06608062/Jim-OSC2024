#ifndef MBOX_H
#define MBOX_H

#include "gpio.h"

#define MBOX_REQUEST 0

/* channels */
#define MBOX_CH_POWER 0
#define MBOX_CH_FB 1
#define MBOX_CH_VUART 2
#define MBOX_CH_VCHIQ 3
#define MBOX_CH_LEDS 4
#define MBOX_CH_BTNS 5
#define MBOX_CH_TOUCH 6
#define MBOX_CH_COUNT 7
#define MBOX_CH_PROP 8

/* tags */
#define MBOX_TAG_GETSERIAL 0x00010004
#define MBOX_TAG_GETBOARD 0x00010002
#define MBOX_TAG_GETARMMEM 0x00010005
#define MBOX_TAG_LAST 0x00000000

#define VIDEOCORE_MBOX (MMIO_BASE + 0x0000B880)
// Mailbox read register
#define MBOX_READ ((volatile unsigned int *)(VIDEOCORE_MBOX + 0x0))
// Mailbox poll register
#define MBOX_POLL ((volatile unsigned int *)(VIDEOCORE_MBOX + 0x10))
// Mailbox sender register
#define MBOX_SENDER ((volatile unsigned int *)(VIDEOCORE_MBOX + 0x14))
// Mailbox status register
#define MBOX_STATUS ((volatile unsigned int *)(VIDEOCORE_MBOX + 0x18))
// Mailbox configuration register
#define MBOX_CONFIG ((volatile unsigned int *)(VIDEOCORE_MBOX + 0x1C))
// Mailbox write register
#define MBOX_WRITE ((volatile unsigned int *)(VIDEOCORE_MBOX + 0x20))
#define MBOX_RESPONSE 0x80000000 // Mailbox response mask
#define MBOX_FULL 0x80000000     // Mailbox full mask
#define MBOX_EMPTY 0x40000000    // Mailbox empty mask

int mbox_call(unsigned char ch);
void mbox_get_board_revision();
void mbox_get_arm_memory();

#endif /* MBOX_H */