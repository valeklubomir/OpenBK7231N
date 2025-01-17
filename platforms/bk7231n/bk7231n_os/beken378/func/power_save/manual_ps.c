#include "manual_ps.h"
#include "manual_ps_pub.h"
#include "gpio_pub.h"
#include "power_save_pub.h"
#include "sys_ctrl_pub.h"
#include "target_util_pub.h"
#include "start_type_pub.h"
#include "sys_ctrl.h"
#include "ps_debug_pub.h"
#include "icu_pub.h"
#include "pwm_pub.h"
#include "BkDriverGpio.h"
#include "phy_trident.h"

void bk_wlan_ps_wakeup_with_timer(UINT32 sleep_time);
void bk_wlan_ps_wakeup_with_peri(UINT8 uart2_wk, UINT32 gpio_index_map);
void power_save_wakeup_with_peri(UINT8 uart2_wk, UINT32 gpio_index_map);
void deep_sleep_wakeup_with_timer(UINT32 sleep_time);

#if PS_SUPPORT_MANUAL_SLEEP
/** @brief  Request power save,and wakeup some time later
 *  @param  sleep_time: Sleep time with milliseconds.
 *              if 0xffffffff not wakeup
 */
void bk_wlan_ps_wakeup_with_timer(UINT32 sleep_time)
{
	PS_DEEP_CTRL_PARAM deep_sleep_param;

	deep_sleep_param.sleep_time     		= sleep_time;
	deep_sleep_param.wake_up_way     		= PS_DEEP_WAKEUP_RTC;
	
		os_printf("---deep sleep test param : 0x%0X 0x%0X 0x%0X 0x%0X %d %d\r\n", 
					deep_sleep_param.gpio_index_map, 
					deep_sleep_param.gpio_edge_map,
					deep_sleep_param.gpio_last_index_map, 
					deep_sleep_param.gpio_last_edge_map,
					deep_sleep_param.sleep_time,
					deep_sleep_param.wake_up_way);
		
    bk_enter_deep_sleep_mode(&deep_sleep_param);
}
void bk_enter_deep_sleep(UINT32 g_gpio_index_map, UINT32 g_gpio_edge_map)
{
	PS_DEEP_CTRL_PARAM deep_sleep_param;

	deep_sleep_param.wake_up_way     		= PS_DEEP_WAKEUP_GPIO;
        deep_sleep_param.gpio_index_map = g_gpio_index_map;
	deep_sleep_param.gpio_edge_map = g_gpio_edge_map;
	
		os_printf("---deep sleep test param : 0x%0X 0x%0X 0x%0X 0x%0X %d %d\r\n", 
					deep_sleep_param.gpio_index_map, 
					deep_sleep_param.gpio_edge_map,
					deep_sleep_param.gpio_last_index_map, 
					deep_sleep_param.gpio_last_edge_map,
					deep_sleep_param.sleep_time,
					deep_sleep_param.wake_up_way);
		
    bk_enter_deep_sleep_mode(&deep_sleep_param);
}


/** @brief  Request power save,and wakeup by uart if uart2_wk=1,nd wakeup by gpio from bitmap of gpio_index_map.
 */
void bk_wlan_ps_wakeup_with_peri( UINT8 uart2_wk, UINT32 gpio_index_map)
{
    power_save_wakeup_with_peri(uart2_wk, gpio_index_map);
}

void power_save_wakeup_with_peri( UINT8 uart2_wk, UINT32 gpio_index_map)
{
    UINT32 reg;
    UINT32 param = 0;
    UINT32 i;
    UINT32 gpio_stat_cfg[32];

    if(power_save_ps_mode_get() != PS_NO_PS_MODE)
    {
        os_printf("can't peri ps,ps in mode %d!\r\n", power_save_ps_mode_get());
        return ;
    }

    for (i = 0; i < GPIONUM; i++)
    {
        gpio_stat_cfg[i] = REG_READ(0x00802800UL + i * 4UL);

        if (gpio_index_map & (0x01UL << i))
        {
            bk_gpio_config_input_pdwn(i);
            BkGpioEnableIRQ(i, 0x2, NULL, NULL);
            os_printf("set peri wkup gpio %d\r\n", i);
        }
    }

#if(PS_XTAL26M_EN == 1)
    reg = REG_READ(SCTRL_LOW_PWR_CLK);
    reg &= ~(LPO_CLK_MUX_MASK);
    reg |= (LPO_SRC_32K_DIV << LPO_CLK_MUX_POSI);
    REG_WRITE(SCTRL_LOW_PWR_CLK, reg);
#endif
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    PS_DEBUG_DOWN_OUT;
    PS_DEBUG_UP_OUT;

    if(uart2_wk == 1)
    {
        param |= ( UART2_ARM_WAKEUP_EN_BIT);
        os_printf("set peri wkup uart2\r\n");
    }

    if(gpio_index_map)
        param |= ( GPIO_ARM_WAKEUP_EN_BIT);

    os_printf("enter peri ps\r\n");
    sddev_control(ICU_DEV_NAME, CMD_ARM_WAKEUP, &param);
    power_save_ps_mode_set(PS_MCU_PS_MODE);
    param = (0xfffff & ~PWD_UART2_CLK_BIT);
    sddev_control(SCTRL_DEV_NAME, CMD_SCTRL_NORMAL_SLEEP, &param);
    delay(200000);
    sddev_control(SCTRL_DEV_NAME, CMD_SCTRL_NORMAL_WAKEUP, 0);

    for (i = 0; i < GPIONUM; i++)
    {
        if (gpio_index_map & (0x01UL << i))
        {
            bk_gpio_config_input_pdwn(i);
            BkGpioDisableIRQ(i);
        }

        REG_WRITE(0x00802800UL + i * 4UL, gpio_stat_cfg[i]);
    }

    power_save_ps_mode_set(PS_NO_PS_MODE);
    GLOBAL_INT_RESTORE();
    os_printf("exit peri ps\r\n");
}


void power_save_timer1_isr(UINT8 param)
{
    PS_DEBUG_PWM_OUT;
}


void power_save_timer1_init()
{
    UINT32 ret;
    pwm_param_t param;
    param.channel         = PWM1;
    param.cfg.bits.en     = PWM_DISABLE;
    param.cfg.bits.int_en = PWM_INT_EN;
    param.cfg.bits.mode   = PWM_TIMER_MODE;
    param.cfg.bits.clk    = PWM_CLK_32K;
    param.p_Int_Handler   = power_save_timer1_isr;
    param.duty_cycle1      = 0x10;
    param.end_value       = 3276;
    ret = sddev_control(PWM_DEV_NAME, CMD_PWM_INIT_PARAM, &param);
    ASSERT(PWM_SUCCESS == ret);
}

void power_save_wakeup_with_timer(UINT32 sleep_time)
{
    UINT32 reg;
    UINT32 param;
    UINT32 wakeup_timer;

    if(power_save_ps_mode_get() != PS_NO_PS_MODE)
    {
        os_printf("can't pwm ps,ps in mode %d!\r\n", power_save_ps_mode_get());
        return ;
    }

    if(sleep_time != 0xffffffff)
    {
        os_printf("sleep with pwm,%d ms\r\n", sleep_time);
#if 1
        reg = REG_READ(SCTRL_LOW_PWR_CLK);
        reg &= ~(LPO_CLK_MUX_MASK);
        reg |= (LPO_SRC_ROSC << LPO_CLK_MUX_POSI);
        REG_WRITE(SCTRL_LOW_PWR_CLK, reg);
#endif
        power_save_timer1_init();
        wakeup_timer = ((sleep_time * 102400) / 3125) ;

        if(wakeup_timer > 65535) //only 16 bit
            wakeup_timer = 65535;
        else if(wakeup_timer < 32)
            wakeup_timer = 32;

        delay(5);
//        power_save_pwm1_enable(wakeup_timer);
    }
    else
    {
        os_printf("sleep forever\r\n");
    }

    os_printf("enter pwm ps\r\n");
    param = PWM_ARM_WAKEUP_EN_BIT;
    sddev_control(ICU_DEV_NAME, CMD_ARM_WAKEUP, &param);
    power_save_ps_mode_set(PS_STANDBY_PS_MODE);
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    PS_DEBUG_DOWN_OUT;
    param = (0xfffff & (~PWD_PWM1_CLK_BIT) & (~PWD_UART2_CLK_BIT));
    sddev_control(SCTRL_DEV_NAME, CMD_SCTRL_RTOS_IDLE_SLEEP, &param);
    PS_DEBUG_UP_OUT;
    delay(5);
    sddev_control(SCTRL_DEV_NAME, CMD_SCTRL_RTOS_IDLE_WAKEUP, 0);
    phy_init_after_wakeup();
    GLOBAL_INT_RESTORE();
    power_save_ps_mode_set(PS_NO_PS_MODE);
//   power_save_pwm1_disable();
    os_printf("exit pwm ps\r\n");
}


void power_save_wakeup_with_gpio(UINT32 gpio_index)
{
    UINT32 reg;
    UINT32 param;

    if(power_save_ps_mode_get() != PS_NO_PS_MODE)
    {
        os_printf("can't gpio ps,ps in mode %d!\r\n", power_save_ps_mode_get());
        return ;
    }

    os_printf("enter gpio ps\r\n");
    bk_gpio_config_input_pdwn(gpio_index);
    BkGpioEnableIRQ(gpio_index, 0x2, NULL, NULL);
    reg = REG_READ(SCTRL_LOW_PWR_CLK);
    reg &= ~(LPO_CLK_MUX_MASK);
    reg |= (LPO_SRC_ROSC << LPO_CLK_MUX_POSI);
    REG_WRITE(SCTRL_LOW_PWR_CLK, reg);
    param = (GPIO_ARM_WAKEUP_EN_BIT);
    sddev_control(ICU_DEV_NAME, CMD_ARM_WAKEUP, &param);
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    PS_DEBUG_DOWN_OUT;
    PS_DEBUG_UP_OUT;
    param = (0xfffff & (~PWD_UART2_CLK_BIT));
    sddev_control(SCTRL_DEV_NAME, CMD_SCTRL_RTOS_IDLE_SLEEP, &param);
    delay(5);
    sddev_control(SCTRL_DEV_NAME, CMD_SCTRL_RTOS_IDLE_WAKEUP, 0);
    phy_init_after_wakeup();
    GLOBAL_INT_RESTORE();
    os_printf("exit gpio ps\r\n");
}

#endif

#if CFG_USE_DEEP_PS

void bk_enter_deep_sleep_mode ( PS_DEEP_CTRL_PARAM *deep_param )
{
	UINT32 param;
	UINT32 i;
	ASSERT ( deep_param != NULL );
	
	if ( ( deep_param->wake_up_way & PS_DEEP_WAKEUP_GPIO ) ) {
		if ( deep_param->gpio_index_map ) {
			bk_printf ( "---enter deep sleep :wake up with gpio 0~31 ps: 0x%x 0x%x \r\n",
			            deep_param->gpio_index_map, deep_param->gpio_edge_map );
		}
		
		if ( deep_param->gpio_last_index_map ) {
			bk_printf ( "---enter deep sleep :wake up with gpio32~39 ps: 0x%x 0x%x \r\n",
			            deep_param->gpio_last_index_map, deep_param->gpio_last_edge_map );
		}
	}
	
	if ( ( deep_param->wake_up_way & PS_DEEP_WAKEUP_RTC ) ) {
		bk_printf ( "---enter deep sleep :wake up with " );
		
		if ( deep_param->lpo_32k_src == LPO_SELECT_32K_XTAL ) {
			bk_printf ( " xtal 32k " );
		}
		else {
			bk_printf ( "  rosc " );
		}
		
		bk_printf ( " ps :%d s\r\n", deep_param->sleep_time );
		
		if ( deep_param->sleep_time > 0x1ffff ) {
			deep_param->sleep_time = 0x1ffff;
		}
		
		deep_param->sleep_time = 32768 * deep_param->sleep_time;
	}

    bk_misc_update_set_type(RESET_SOURCE_DEEPPS_GPIO);
	
	GLOBAL_INT_DECLARATION();
	GLOBAL_INT_DISABLE();
	sddev_control ( SCTRL_DEV_NAME, CMD_SCTRL_RTOS_DEEP_SLEEP, deep_param );
	delay ( 5 );
	GLOBAL_INT_RESTORE();
	(void)i;
	(void)param;
}

#endif

