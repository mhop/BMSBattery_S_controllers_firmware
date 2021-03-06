/*
 * EGG OpenSource EBike firmware
 *
 * Copyright (C) Casainho, 2015, 2106, 2017.
 *
 * Released under the GPL License, Version 3
 */

#include <stdint.h>
#include <stdio.h>
#include "stm8s_gpio.h"
#include "stm8s_tim1.h"
#include "gpio.h"
#include "pwm.h"
#include "config.h"
#include "adc.h"
#include "motor_controller_low_level.h"
#include "motor_controller_high_level.h"
#include "communications_controller.h"

uint16_t ui16_PWM_cycles_counter = 0;
uint16_t ui16_PWM_cycles_counter_6 = 0;
uint16_t ui16_PWM_cycles_counter_total = 0;

uint16_t ui16_motor_speed_erps = 0;
uint8_t ui8_motor_rotor_position = 0; // in 360/256 degrees
uint8_t ui8_motor_rotor_absolute_position = 0; // in 360/256 degrees
uint8_t ui8_position_correction_value = 127; // in 360/256 degrees
uint8_t ui8_interpolation_angle = 0;

uint8_t ui8_motor_commutation_type = BLOCK_COMMUTATION;
uint8_t ui8_motor_state = MOTOR_STATE_COAST;
uint8_t ui8_motor_controller_state = MOTOR_CONTROLLER_STATE_OK;

uint8_t ui8_hall_sensors = 0;
uint8_t ui8_hall_sensors_last = 0;

uint8_t ui8_ADC_id_current = 0;

uint8_t ui8_adc_motor_current_max;
uint8_t ui8_adc_motor_regen_current_max;

uint8_t ui8_adc_motor_total_current;
uint8_t ui8_motor_total_current_offset;
uint16_t ui16_motor_total_current_offset_10b;

uint8_t ui8_half_e_rotation_flag = 0;

void TIM1_UPD_OVF_TRG_BRK_IRQHandler(void) __interrupt(TIM1_UPD_OVF_TRG_BRK_IRQHANDLER)
{
  adc_trigger ();

  hall_sensors_read_and_action ();

  motor_fast_loop ();

  TIM1_ClearITPendingBit(TIM1_IT_UPDATE);
}

void hall_sensors_read_and_action (void)
{
  // read hall sensors signal pins and mask other pins
  ui8_hall_sensors = (GPIO_ReadInputData (HALL_SENSORS__PORT) & (HALL_SENSORS_MASK));
  if (ui8_hall_sensors != ui8_hall_sensors_last)
  {
    ui8_hall_sensors_last = ui8_hall_sensors;

    switch (ui8_hall_sensors)
    {
      case 3:
      if (ui8_motor_commutation_type == BLOCK_COMMUTATION)
      {
	pwm_phase_a_enable_pwm (ui8_duty_cycle);
	pwm_phase_b_disable (ui8_duty_cycle);
	pwm_phase_c_enable_low (ui8_duty_cycle);
      }
      else if (ui8_motor_commutation_type == SINEWAVE_INTERPOLATION_60_DEGREES)
      {
	ui8_motor_rotor_absolute_position = ANGLE_180 + MOTOR_ROTOR_DELTA_PHASE_ANGLE_RIGHT;
      }
      break;

      case 1:
      if (ui8_half_e_rotation_flag == 1)
      {
	ui8_half_e_rotation_flag = 0;
	ui16_PWM_cycles_counter_total = ui16_PWM_cycles_counter;
	ui16_PWM_cycles_counter = 0;
	ui16_motor_speed_erps = PWM_CYCLES_SECOND / ui16_PWM_cycles_counter_total; // this division takes ~4.2us
      }

      // update motor commutation state based on motor speed
#if (MOTOR_TYPE == MOTOR_TYPE_Q85) || (MOTOR_TYPE == MOTOR_TYPE_Q100)
#ifdef DO_SINEWAVE_INTERPOLATION_360_DEGREES
      if ((ui16_motor_speed_erps > 120) &&
	  (ui8_motor_commutation_type == SINEWAVE_INTERPOLATION_60_DEGREES))
      {

	ui8_motor_commutation_type = SINEWAVE_INTERPOLATION_360_DEGREES;
	ui8_motor_state = MOTOR_STATE_RUNNING;
      }
      if ((ui16_motor_speed_erps < 90) &&
	  (ui8_motor_commutation_type == SINEWAVE_INTERPOLATION_360_DEGREES))
      {
	ui8_motor_commutation_type = SINEWAVE_INTERPOLATION_60_DEGREES;
	ui8_motor_state = MOTOR_STATE_RUNNING;
      }
#endif
      if ((ui16_motor_speed_erps > 80) &&
	  (ui8_motor_commutation_type == BLOCK_COMMUTATION))
      {
	ui8_motor_commutation_type = SINEWAVE_INTERPOLATION_60_DEGREES;
	ui8_motor_state = MOTOR_STATE_RUNNING;

	pwm_init_bipolar_4q ();
      }
#elif MOTOR_TYPE == MOTOR_TYPE_EUC2
      if (ui16_motor_speed_erps > 3)
      {
	ui8_motor_commutation_type = SINEWAVE_INTERPOLATION_60_DEGREES;
	ui8_motor_state = MOTOR_STATE_RUNNING;
      }
      else
      {
	ui8_motor_commutation_type = BLOCK_COMMUTATION;
      }
#endif

      if (ui8_motor_commutation_type == BLOCK_COMMUTATION)
      {
	pwm_phase_a_enable_pwm (ui8_duty_cycle);
	pwm_phase_b_enable_low (ui8_duty_cycle);
	pwm_phase_c_disable (ui8_duty_cycle);
      }
      else
      {
	ui8_motor_rotor_absolute_position = ANGLE_240 + MOTOR_ROTOR_DELTA_PHASE_ANGLE_RIGHT;
      }
      break;

      case 5:
      if (ui8_motor_commutation_type == BLOCK_COMMUTATION)
      {
	pwm_phase_a_disable (ui8_duty_cycle);
	pwm_phase_b_enable_low (ui8_duty_cycle);
	pwm_phase_c_enable_pwm (ui8_duty_cycle);
      }
      else if (ui8_motor_commutation_type == SINEWAVE_INTERPOLATION_60_DEGREES)
      {
	ui8_motor_rotor_absolute_position = ANGLE_300 + MOTOR_ROTOR_DELTA_PHASE_ANGLE_RIGHT;
      }
      break;

      case 4:
      // read here the phase B current: FOC Id current
      ui8_ADC_id_current = ui8_adc_read_phase_B_current ();

#if (MOTOR_TYPE == MOTOR_TYPE_EUC2)
      if (ui8_motor_state == MOTOR_STATE_RUNNING)
      {
	if (ui8_motor_commutation_type == SINEWAVE_INTERPOLATION_60_DEGREES)
	{
	  if (ui8_ADC_id_current > 127)
	  {
	    ui8_position_correction_value++;
	  }
	  else if (ui8_ADC_id_current < 125)
	  {
	    ui8_position_correction_value--;
	  }
	}
      }
#elif (MOTOR_TYPE == MOTOR_TYPE_Q85) || (MOTOR_TYPE == MOTOR_TYPE_Q100)
      if (ui8_motor_state == MOTOR_STATE_RUNNING)
      {
	if ((ui8_motor_commutation_type == SINEWAVE_INTERPOLATION_60_DEGREES) ||
	   (ui8_motor_commutation_type == SINEWAVE_INTERPOLATION_360_DEGREES))
	{
	  if (ui8_ADC_id_current > 112)
	  {
	    ui8_position_correction_value--;
	  }
	  else if (ui8_ADC_id_current < 110)
	  {
	    ui8_position_correction_value++;
	  }
	}
	else
	{
	  ui8_position_correction_value = 127; // keep using the reset value
	}
      }
#endif

      if (ui8_motor_commutation_type == BLOCK_COMMUTATION)
      {
	pwm_phase_a_enable_low (ui8_duty_cycle);
	pwm_phase_b_disable (ui8_duty_cycle);
	pwm_phase_c_enable_pwm (ui8_duty_cycle);
      }
      else if (ui8_motor_commutation_type == SINEWAVE_INTERPOLATION_60_DEGREES)
      {
	ui8_motor_rotor_absolute_position = ANGLE_1 + MOTOR_ROTOR_DELTA_PHASE_ANGLE_RIGHT;
      }
      break;

      case 6:
      ui8_half_e_rotation_flag = 1;

      if (ui8_motor_commutation_type == BLOCK_COMMUTATION)
      {
	pwm_phase_a_enable_low (ui8_duty_cycle);
	pwm_phase_b_enable_pwm (ui8_duty_cycle);
	pwm_phase_c_disable (ui8_duty_cycle);
      }
      else if (ui8_motor_commutation_type == SINEWAVE_INTERPOLATION_60_DEGREES)
      {
	ui8_motor_rotor_absolute_position = ANGLE_60 + MOTOR_ROTOR_DELTA_PHASE_ANGLE_RIGHT;
      }
      break;

      case 2:
      if (ui8_motor_commutation_type == BLOCK_COMMUTATION)
      {
	pwm_phase_a_disable (ui8_duty_cycle);
	pwm_phase_b_enable_pwm (ui8_duty_cycle);
	pwm_phase_c_enable_low (ui8_duty_cycle);
      }
      else if (ui8_motor_commutation_type == SINEWAVE_INTERPOLATION_60_DEGREES)
      {
	ui8_motor_rotor_absolute_position = ANGLE_120 + MOTOR_ROTOR_DELTA_PHASE_ANGLE_RIGHT;
      }
      break;

      default:
      return;
      break;
    }

    ui16_PWM_cycles_counter_6 = 0;
  }
}

// runs every 64us (PWM frequency)
void motor_fast_loop (void)
{
  // count number of fast loops / PWM cycles
  if (ui16_PWM_cycles_counter < PWM_CYCLES_COUNTER_MAX)
  {
    ui16_PWM_cycles_counter++;
    ui16_PWM_cycles_counter_6++;
  }
  else
  {
    ui16_PWM_cycles_counter = 0;
    ui16_PWM_cycles_counter_6 = 0;
    ui16_motor_speed_erps = 0;
    ui16_PWM_cycles_counter_total = 0xffff;
    ui8_position_correction_value = 127;
    ui8_motor_commutation_type = BLOCK_COMMUTATION;
    ui8_motor_state = MOTOR_STATE_STOP;
    pwm_init_6_steps ();
    ui8_hall_sensors_last = 0; // this way we force execution of hall sensors code
  }

#define DO_INTERPOLATION 1 // may be usefull to disable interpolation when debugging
#if DO_INTERPOLATION == 1
  // calculate the interpolation angle
  // interpolation seems a problem when motor starts, so avoid to do it at very low speed
  if (ui8_motor_commutation_type == SINEWAVE_INTERPOLATION_60_DEGREES)
  {
    ui8_interpolation_angle = (ui16_PWM_cycles_counter_6 << 8) / ui16_PWM_cycles_counter_total;
#if (MOTOR_TYPE == MOTOR_TYPE_Q85) || (MOTOR_TYPE == MOTOR_TYPE_Q100)
    ui8_motor_rotor_position = ui8_motor_rotor_absolute_position + ui8_position_correction_value + ui8_interpolation_angle;
#elif MOTOR_TYPE == MOTOR_TYPE_EUC2
    ui8_motor_rotor_position = ui8_motor_rotor_absolute_position + ui8_position_correction_value - ui8_interpolation_angle;
#endif
  }
  else if (ui8_motor_commutation_type == SINEWAVE_INTERPOLATION_360_DEGREES)
  {
    ui8_interpolation_angle = (ui16_PWM_cycles_counter << 8) / ui16_PWM_cycles_counter_total;
#if (MOTOR_TYPE == MOTOR_TYPE_Q85) || (MOTOR_TYPE == MOTOR_TYPE_Q100)
    ui8_motor_rotor_position = ui8_motor_rotor_absolute_position + ui8_position_correction_value + ui8_interpolation_angle;
#elif MOTOR_TYPE == MOTOR_TYPE_EUC2
    ui8_motor_rotor_position = ui8_motor_rotor_absolute_position + ui8_position_correction_value - ui8_interpolation_angle;
#endif
  }
  else
#endif
  {
    ui8_motor_rotor_position = ui8_motor_rotor_absolute_position;
  }

  pwm_duty_cycle_controller ();
}

void motor_disable_PWM (void)
{
  TIM1_CtrlPWMOutputs(DISABLE);
}

void motor_enable_PWM (void)
{
  TIM1_CtrlPWMOutputs(ENABLE);
}

void motor_controller_set_state (uint8_t state)
{
  ui8_motor_controller_state |= state;
}

void motor_controller_reset_state (uint8_t state)
{
  ui8_motor_controller_state &= ~state;
}

uint8_t motor_controller_get_state (void)
{
  return ui8_motor_controller_state;
}

uint8_t motor_controller_state_is_set (uint8_t state)
{
  return ui8_motor_controller_state & state;
}

void hall_sensor_init (void)
{
  GPIO_Init(HALL_SENSORS__PORT,
	    (GPIO_Pin_TypeDef)(HALL_SENSOR_A__PIN | HALL_SENSOR_B__PIN | HALL_SENSOR_C__PIN),
	    GPIO_MODE_IN_FL_NO_IT);
}

void motor_init (void)
{
  /***************************************************************************************/
  // motor overcurrent pin as external input pin interrupt
  GPIO_Init(CURRENT_MOTOR_TOTAL_OVER__PORT,
	    CURRENT_MOTOR_TOTAL_OVER__PIN,
	    GPIO_MODE_IN_FL_IT); // with external interrupt

  //initialize the Interrupt sensitivity
  EXTI_SetExtIntSensitivity(EXTI_PORT_GPIOD,
			    EXTI_SENSITIVITY_FALL_LOW);
  /***************************************************************************************/

  motor_set_current_max (ADC_MOTOR_CURRENT_MAX);
  motor_set_regen_current_max (ADC_MOTOR_REGEN_CURRENT_MAX);
  motor_set_pwm_duty_cycle_ramp_inverse_step (PWM_DUTY_CYCLE_RAMP_INVERSE_STEP); // each step = 64us
}

void motor_set_pwm_duty_cycle_target (uint8_t value)
{
  if (value > PWM_VALUE_DUTY_CYCLE_MAX) { value = PWM_VALUE_DUTY_CYCLE_MAX; }

  pwm_set_duty_cycle_target (value);
}

void motor_set_current_max (uint8_t value)
{
  ui8_adc_motor_current_max = value;
}

uint8_t motor_get_current_max (void)
{
  return ui8_adc_motor_total_current - ui8_motor_total_current_offset;
}

void motor_set_regen_current_max (uint8_t value)
{
  ui8_adc_motor_regen_current_max = value;
}

void motor_set_pwm_duty_cycle_ramp_inverse_step (uint16_t value)
{
  ui16_duty_cycle_ramp_inverse_step = value;
}

uint16_t motor_get_motor_speed_erps (void)
{
  return ui16_motor_speed_erps;
}

uint16_t motor_get_er_PWM_ticks (void)
{
  return ui16_PWM_cycles_counter_total;
}

// motor overcurrent interrupt
void EXTI_PORTD_IRQHandler(void) __interrupt(EXTI_PORTD_IRQHANDLER)
{
  // motor will stop and error symbol on LCD will be shown
  motor_controller_set_state (MOTOR_CONTROLLER_STATE_OVER_CURRENT);
  motor_disable_PWM ();
  motor_controller_set_error (MOTOR_CONTROLLER_ERROR_06_SHORT_CIRCUIT);
}
