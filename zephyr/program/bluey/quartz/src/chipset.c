/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quartz 芯片组特定配置
 *
 * 本文件定义了 Quartz 平台在开机/关机过程中的 GPIO 控制逻辑
 * 这些钩子函数会在 AP 电源状态转换时被调用
 *
 * 电源状态流程:
 * G3 (完全断电) -> S5 (软关机) -> S4 (休眠) -> S3 (睡眠) -> S0 (运行)
 *
 * 涉及的电源轨:
 * - PP5000: 主电源轨 (5V)
 * - PP3300: 3.3V 电源
 * - PP1800: 1.8V 电源
 */

#include "battery.h"
#include "common.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

/*
 * ============================================================================
 * 延迟关闭函数: disable_pp5000_s5()
 * ============================================================================
 * 延迟关闭 PP5000_S5 电源轨
 *
 * PP5000_S5 是 S5 状态下的主电源轨，在系统完全关机后需要延迟关闭
 * 这样可以确保系统有足够时间完成关机流程
 *
 * 使用场景:
 * - 当系统进入 S5 软关机状态时
 * - 在延迟 5 秒后自动关闭 PP5000_S5
 */
static void disable_pp5000_s5(void)
{
	/* 关闭 PP5000_S5 电源轨 (设为低电平) */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5), 0);
}
/* 声明为延迟调用，在需要时通过 hook_call_deferred() 调度 */
DECLARE_DEFERRED(disable_pp5000_s5);

/*
 * ============================================================================
 * 开机启动钩子: board_chipset_startup_quartz()
 * ============================================================================
 * HOOK_CHIPSET_STARTUP 回调，在 AP 开机过程中调用
 *
 * 此时 AP 已经开始启动，EC 需要启用各种外设和电源轨
 * 这些外设包括: 触觉马达、触摸板、显示屏、风扇等
 */
void board_chipset_startup_quartz(void)
{
	/* 启用触觉马达电源 (用于触控反馈) */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec), 1);

	/* 启用触摸板电源 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en), 1);

	/* 打开背光 (设为高电平，取消 OD 门输出) */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl), 1);

	/* 启用风扇电源 (PP5000_FAN) */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 1);

	/* 启用 OLED 屏幕 AVDD 电源 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_enavdd_oled), 1);
}
/* 注册开机启动钩子，优先级为默认 */
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup_quartz,
	     HOOK_PRIO_DEFAULT);

/*
 * ============================================================================
 * 关机钩子: board_chipset_shutdown_quartz()
 * ============================================================================
 * HOOK_CHIPSET_SHUTDOWN 回调，在 AP 关机过程中调用
 *
 * 关闭所有在开机时启用的外设，节省电量
 * 注意: 不关闭 PP5000_S5，因为它在 S5 状态下仍然需要供电
 */
void board_chipset_shutdown_quartz(void)
{
	/* 关闭触觉马达电源 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec), 0);

	/* 关闭触摸板电源 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en), 0);

	/* 关闭背光 (设为低电平，OD 门拉低) */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl), 0);

	/* 关闭风扇电源 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);

	/* 关闭 OLED 屏幕 AVDD 电源 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_enavdd_oled), 0);

	/* 关闭 CPU 节流信号 (如果有) */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_throttle_mira), 0);
}
/* 注册关机钩子 */
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown_quartz,
	     HOOK_PRIO_DEFAULT);

/*
 * ============================================================================
 * S3 电源中断处理: s3_power_interrupt()
 * ============================================================================
 * S3 (睡眠) 状态的电源监控中断处理函数
 *
 * 这个函数响应 S3 电源监控信号，根据 PP1800_L1I_S3_EC 的状态
 * 来控制 PP3300_S3 电源轨
 *
 * 工作原理:
 * - 当 PP1800_L1I_S3_EC 为高电平时，表示 S3 电源路径就绪
 *   此时启用 PP3300_S3，为睡眠状态下的相关电路供电
 * - 当 PP1800_L1I_S3_EC 为低电平时，关闭 PP3300_S3
 *
 * 注意: 这是一个 GPIO 中断处理函数，每次 S3 信号变化时都会被调用
 */
void s3_power_interrupt(enum gpio_signal signal)
{
	/* 读取 S3 电源监控信号状态，并设置 PP3300_S3 输出 */
	gpio_pin_set_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp3300_s3),
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pp1800_l1i_s3_ec)));
}

/*
 * ============================================================================
 * 启用 S3 中断: enable_s3_interrupt()
 * ============================================================================
 * HOOK_INIT 回调，在系统初始化时调用
 *
 * 启用 S3 电源监控 GPIO 中断，使系统能够检测 S3 状态变化
 */
static void enable_s3_interrupt(void)
{
	/* 启用 int_s3_power_monitor 中断 */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_s3_power_monitor));
}
/* 注册初始化钩子 */
DECLARE_HOOK(HOOK_INIT, enable_s3_interrupt, HOOK_PRIO_DEFAULT);

/*
 * ============================================================================
 * 芯片组预初始化钩子: board_chipset_pre_init_quartz()
 * ============================================================================
 * HOOK_CHIPSET_PRE_INIT 回调，在 AP 芯片组的预初始化阶段调用
 *
 * 这个钩子在电源序列的早期阶段调用，用于:
 * 1. 确保 PP5000_S5 电源轨已启用
 * 2. 取消之前调度的延迟关闭操作
 * 3. 如果没有电池，启用 CPU 节流以保护系统
 */
static void board_chipset_pre_init_quartz(void)
{
	/* 取消之前调度的 PP5000_S5 延迟关闭
	 * 参数 -1 表示取消所有挂起的延迟调用
	 * 这确保在开机时 PP5000_S5 保持开启 */
	hook_call_deferred(&disable_pp5000_s5_data, -1);

	/* 启用 PP5000_S5 电源轨 (设为高电平) */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_s5), 1);

	/* 检查电池是否存在 */
	if (battery_is_present() != BP_YES) {
		/* 无电池时启用 CPU 节流
		 * 这是一个安全措施，防止在无电池情况下
		 * 电源意外断开导致数据丢失或硬件损坏 */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_sys_throttle_mira),
				1);
	}
}
/* 注册芯片组预初始化钩子 */
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_chipset_pre_init_quartz,
	     HOOK_PRIO_DEFAULT);

/*
 * ============================================================================
 * 硬关机钩子: board_chipset_hard_off_quartz()
 * ============================================================================
 * HOOK_CHIPSET_HARD_OFF 回调，在系统进入完全断电状态时调用
 *
 * 此时 AP 已经关闭，EC 调度一个延迟任务在 5 秒后关闭 PP5000_S5
 * 延迟的目的是确保系统有足够时间完成所有关机操作
 *
 * 注意: HOOK_CHIPSET_HARD_OFF 不同于 HOOK_CHIPSET_SHUTDOWN
 * - HOOK_CHIPSET_SHUTDOWN: AP 软关机 (S5)
 * - HOOK_CHIPSET_HARD_OFF: 系统完全断电 (G3)
 */
static void board_chipset_hard_off_quartz(void)
{
	/* 调度延迟关闭 PP5000_S5，延迟 5 秒 (5000 * 1000 微秒) */
	hook_call_deferred(&disable_pp5000_s5_data, 5000 * USEC_PER_MSEC);
}
/* 注册硬关机钩子 */
DECLARE_HOOK(HOOK_CHIPSET_HARD_OFF, board_chipset_hard_off_quartz,
	     HOOK_PRIO_DEFAULT);
