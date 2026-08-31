/**
 * @file rfid.h
 * @brief RFID application-layer entry point.
 *
 * Wraps the board-specific hardware ops (carrier PWM on TIM2_CH3, input
 * capture on TIM15_CH1/CH2) and the OS-abstraction interface into the
 * platform-agnostic rfid_handler. Ported from the STM32L010 (TIM2-only)
 * implementation; the only hardware change is the IC timer (TIM2 -> TIM15).
 */

#ifndef __RFID_H__
#define __RFID_H__

#include "rfid_handler.h"
#include "switch_handler.h"

/**
 * @brief Initialise the RFID decoder.
 *
 * Populates the OS interface table, instantiates the handler, and registers
 * the application decode-complete callback. Called once during system startup
 * (from App_ThreadX_Init).
 */
void rfid_init(void);

/**
 * @brief Application decode-complete callback.
 *
 * Called in the RFID handler thread context whenever a tag is decoded. The
 * test hook stamps byte[3] so a read can be verified on the logic analyser.
 *
 * @param rfid  Decoded 5-byte tag data.
 * @param arg   User argument (unused).
 */
void rfid_cpl_callback(uint8_t *rfid, void *arg);

void ic_isr_cb(void);

/*
 * Trigger an asynchronous RFID read started by a hall2 edge.
 * Retries automatically on read failure (all-0xFF) up to RFID_READ_MAX_ATTEMPTS
 * total attempts, then forwards edge + rfid to wapi_commu_task. Safe to call from
 * the switch handler thread; edges arriving while a read is in progress are
 * ignored.
 */
void rfid_start_with_retry(switch_edge_t edge);

/* -------------------------------------------------------------------------- */
/*                    Low-power enter / exit hooks                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Prepare the RFID module for low-power sleep.
 *
 * Stops the carrier PWM and input-capture timer to minimise sleep current.
 */
void rfid_enter_low_power(void);

/**
 * @brief Restore the RFID module after waking from low-power sleep.
 *
 * Re-starts the carrier PWM and input-capture timer so tag reads can resume.
 */
void rfid_exit_low_power(void);

#endif /* __RFID_H__ */
