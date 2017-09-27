/*
 * EGG OpenSource EBike firmware
 *
 * Copyright (C) Casainho, 2015, 2106, 2017.
 *
 * Released under the GPL License, Version 3
 */

#ifndef _MOTOR_H
#define _MOTOR_H

#include "main.h"
#include "interrupts.h"

// motor states
#define NO_INTERPOLATION_60_DEGREES 2
#define INTERPOLATION_60_DEGREES 3
#define INTERPOLATION_360_DEGREES 4

#define MOTOR_STATE_STOP 0
#define MOTOR_STATE_COAST 1
#define MOTOR_STATE_RUNNING 2

extern uint8_t ui8_motor_rotor_position;
extern uint8_t ui8_position_correction_value;
extern uint8_t ui8_position_correction_value1;
extern uint16_t ui16_speed_inverse;
extern uint8_t ui8_motor_interpolation_state;
extern uint16_t ui16_PWM_cycles_counter_total;
extern uint16_t ui16_motor_speed_erps;
extern uint8_t ui8_ADC_id_current;
extern uint8_t ui8_motor_total_current_flag;
extern uint8_t ui8_motor_current;
extern uint16_t ui16_motor_current;
extern uint8_t ui8_ADC_id_current_sign;

extern uint16_t ui16_ADC_id_current_accumulated;

void hall_sensor_init (void);
void hall_sensors_read_and_action (void);
void motor_init (void);
void motor_fast_loop (void);
void motor_set_mode_coast (void);
void motor_set_mode_run (void);

#endif /* _MOTOR_H_ */
