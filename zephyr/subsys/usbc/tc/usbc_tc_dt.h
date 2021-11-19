/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/*
 * USBC Type-C DeviceTree
 */
#ifndef _USBC_TC_DT_H_
#define _USBC_TC_DT_H_

/*****************************************************************************
 * DeviceTree helper macros
 *
 * The following DeviceTree example describes a three USB-C port system.
 *	USB-C0 will be Sink only and uses TCPC as it will be defined in
 *	tcpc_port_0.
 *
 *	USB-C1 will be Sink only with Debug Accessory and uses TCPC as it
 *	will be defined in tcpc_port_1.
 *
 *	USB-C2 will be Sink only and uses TCPC as it will be defined in
 *	tcpc_port_2.
 *
 *	usbc_port_list {
 *		compatible = "usbc-subsystem-statemachine-port-list";
 *		status = “okay”;
 *
 *		usbc_port_0: usbc_port_0 {
 *			usbc-port = 0;
 *			usbc-tcpc = <&tcpc_port_0>;
 *			usbc-type = USBC_SNK;
 *		};
 *		usbc_port_1: usbc_port_1 {
 *			usbc-port = 1;
 *			usbc-tcpc = <&tcpc_port_1>;
 *			usbc-type = USBC_SNK_DBGACC;
 *		};
 *		usbc_port_2: usbc_port_2 {
 *			usbc-port = 2;
 *			usbc-tcpc = <&tcpc_port_2>;
 *			usbc-type = USBC_SNK;
 *		};
 *	};
 */
/*
 * USBC_PORT_LIST_NODE
 * Return the parent node of the USBC Port List
 */
#define USBC_PORT_LIST_NODE						\
	DT_COMPAT_GET_ANY_STATUS_OKAY(					\
				usbc_subsystem_statemachine_port_list)

/*
 * SYSTEM_DT_USBC_CONFIG
 * Determine if the USBC State Machine is needed
 */
#define SYSTEM_DT_USBC_CONFIG						\
	DT_HAS_COMPAT_STATUS_OKAY(					\
				usbc_subsystem_statemachine_port_list)


/*
 * USBC_SM_IS_TYPE
 * Helper macro to determine if the USBC node is of a given type
 */
#define USBC_STATE_MACHINE_IS_TYPE(node, type)				\
	UTIL_AND(DT_NODE_HAS_PROP(node, usbc_type),			\
		 DT_NODE_PROP(node, usbc_type) == type)


/*
 * USBC_TC_INCLUDES_SNK_STATE_MACHINE
 * Determine if the USBC SNK State Machine is needed
 */
#define USBC_SNK_ENUM_PRESENT_ENTRY(cid)				\
(									\
	UTIL_OR(USBC_STATE_MACHINE_IS_TYPE(cid, USBC_SNK),		\
		USBC_STATE_MACHINE_IS_TYPE(cid, USBC_SNK_DGBACC))	\
) ||

#define USBC_TC_INCLUDES_SNK_STATE_MACHINE				\
(									\
	DT_FOREACH_CHILD(						\
		USBC_PORT_LIST_NODE,					\
		USBC_SNK_ENUM_PRESENT_ENTRY				\
	)								\
	(0)								\
)


/*
 * USBC_TC_INCLUDES_DBGACC_OPTION
 * Determine if the USBC SNK State Machine needs the DebugAccessory
 */
#define USBC_SNK_DBGACC_ENUM_PRESENT_ENTRY(cid)				\
(									\
	USBC_STATE_MACHINE_IS_TYPE(cid, USBC_SNK_DGBACC)		\
) ||

#define USBC_TC_INCLUDES_DBGACC_OPTION					\
(									\
	DT_FOREACH_CHILD(						\
		USBC_PORT_LIST_NODE,					\
		USBC_SNK_DBGACC_ENUM_PRESENT_ENTRY			\
	)								\
	(0)								\
)


/*
 * USBC_TC_INCLUDES_SRC_STATE_MACHINE
 * Determine if the USBC SRC State Machine is needed
 */
#define USBC_SRC_ENUM_PRESENT_ENTRY(cid)				\
(									\
	USBC_STATE_MACHINE_IS_TYPE(cid, USBC_SRC)			\
) ||

#define USBC_TC_INCLUDES_SRC_STATE_MACHINE				\
(									\
	DT_FOREACH_CHILD(						\
		USBC_PORT_LIST_NODE,					\
		USBC_SRC_ENUM_PRESENT_ENTRY				\
	)								\
	(0)								\
)


/*
 * USBC_TC_INCLUDES_DRP_STATE_MACHINE
 * Determine if the USBC DRP State Machine is needed
 */
#define USBC_DRP_ENUM_PRESENT_ENTRY(cid)				\
(									\
	USBC_STATE_MACHINE_IS_TYPE(cid, USBC_DRP)			\
) ||

#define USBC_TC_INCLUDES_DRP_STATE_MACHINE				\
(									\
	DT_FOREACH_CHILD(						\
		USBC_PORT_LIST_NODE,					\
		USBC_DRP_ENUM_PRESENT_ENTRY				\
	)								\
	(0)								\
)

#endif /* _USBC_TC_DT_H_ */
