/*
 * test Driver
 */

#include "drivers/ucsi_v3.h"

#include <assert.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/smbus.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/smf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys_clock.h>

#include "usbc/utils.h"

#include <drivers/pdc.h>

//LOG_MODULE_REGISTER(pdc_anx74, CONFIG_USB_PDC_LOG_LEVEL);
LOG_MODULE_REGISTER(pdc_anx74, LOG_LEVEL_DBG);

//#define ANX_DEBUG_DEF  // default enable debug print

#ifdef ANX_DEBUG_DEF
#define ANX_DEBUG_PRINT_HEAD const struct pdc_config_t *cfg = data->dev->config;
#define ANX_DEBUG(...) LOG_DBG(__VA_ARGS__)
#undef assert
#define assert(expression) while(!(expression)) { \
    LOG_ERR("ASSERT: %s, %s, %d", #expression, __FILE__, __LINE__); \
    k_sleep(K_SECONDS(10)); \
} 
#else
// note!! TODO !! This version is for debug and not pushed to google source yet
//#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#define ANX_DEBUG( ...) 
#define ANX_DEBUG_PRINT_HEAD
#endif

#define ANX_Test_Port_Number_Wraper (data->internal_ucsi_region_p1)


#define DT_DRV_COMPAT analogix_anx74_pdc

#define BYTE0(n) ((n) & 0xff)
#define BYTE1(n) (((n) >> 8) & 0xff)
#define BYTE2(n) (((n) >> 16) & 0xff)
#define BYTE3(n) (((n) >> 24) & 0xff)

/**
 * @brief Time before sending a cci status
 */
#define T_CCI_STATUS 20

/**
 * @brief Error Recovery Delay Counter (time delay is 60mS)
 */
#define N_ERROR_RECOVERY_DELAY_COUNT (60 / T_CCI_STATUS)

/**
 * @brief Max number of error recovery attempts
 */
#define N_MAX_ERROR_RECOVERY_COUNT 4

/**
 * @brief Number of times to try an I2C transaction
 */
#define N_I2C_TRANSACTION_COUNT 10

/**
 * @brief Number of times to send a cci status
 */
#define N_RETRY_COUNT 200

/**
 * @brief Number of times to try and initialize the driver
 */
#define N_INIT_RETRY_ATTEMPT_MAX 2

/**
 * @brief IRQ Event set by the interrupt handler.
 */
#define ANX74XX_IRQ_EVENT BIT(0)

/**
 * @brief Event set to run next state of state machine.
 */
#define anx74xx_NEXT_STATE_READY BIT(1)

/**
 * @brief Number of ANX74XX ports detected
 */
#define NUM_PDC_ANX74XX_PORTS DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

/////////////////////////////////////////////////////////////////////////////////////

#define ANX_PDC_anx74xx_INFO_REG_VID               0x0
#define ANX_PDC_anx74xx_INFO_REG_VIDV                  0x7977
#define ANX_PDC_anx74xx_INFO_REG_PID               0x2

#define ANX_PDC_anx74xx_INFO_REG_PD_VERSION        0x08
#define ANX_PDC_anx74xx_INFO_REG_PD_REVERSION      0x09

#define ANX_PDC_anx74xx_TCPC_REG_TCPC_ANALOG_CTRL               0x0C
#define ANX_PDC_anx74xx_TCPC_REG_TCPC_ANALOG_CTRL_CABLE_DET_DIG      BIT(6)         // When digital_rdy=1, cable_det is controlled by cable_det_dig.
#define ANX_PDC_anx74xx_TCPC_REG_TCPC_ANALOG_CTRL_DIGITAL_RDY        BIT(5)         // 1: digital is ready; 0: digital powered down. When digital_rdy=1, cable_det is controlled by cable_det_dig.

#define ANX_PDC_anx74xx_TCPC_REG_TCPC_ROLE_CONTROL               0x1A
#define ANX_PDC_anx74xx_TCPC_REG_TCPC_ROLE_CONTROL_VAL_DEFAULT               0x5A
#define ANX_PDC_anx74xx_TCPC_REG_TCPC_CMD                        0x23
#define ANX_PDC_anx74xx_TCPC_REG_TCPC_CMD_VAL_LOOK_FOR_CONNECT        0x99
#define ANX_PDC_anx74xx_TCPC_REG_TCPC_CMD_VAL_DEEP_POWER_DOWN         0xff

#define ANX_PDC_anx74xx_TCPC_REG_VBUS_VOLTAGE                     0x70

#define ANX_PDC_anx74xx_TCPC_REG_TCPC_CTRL3                        0xCF
#define ANX_PDC_anx74xx_TCPC_REG_TCPC_CTRL3_VAL_DEFAULT                        0x93
#define ANX_PDC_anx74xx_TCPC_REG_TCPC_CTRL4                        0xD7
#define ANX_PDC_anx74xx_TCPC_REG_TCPC_CTRL4_VAL_DEFAULT                        0x2c

#define ANX_PDC_anx74xx_TCPC_REG_TCPC_PD_DRP               0xF0
#define ANX_PDC_anx74xx_TCPC_REG_TCPC_PD_DRP_VAL_DEFAULT               0x2a

#define ANX_PDC_anx74xx_TOP_REG_FLASH_RAM_CTRL        0x05
#define ANX_PDC_anx74xx_TOP_REG_FLASH_RAM_CTRL_RELOAD  BIT(1)

#define ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE          0x68   
#define ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE_UCSI                        BIT(7)    
#define ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE_IDLE                        BIT(6)
#define ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE_PORT1_SYSTEM_STATUS         BIT(5)
#define ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE_PORT0_SYSTEM_STATUS         BIT(4)
#define ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE_RECEIVED_MSG                BIT(3)
#define ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE_PROCHOT_INT                 BIT(2)
#define ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE_POW_ON                      BIT(1)
#define ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE_POW_OFF                     BIT(0)

#define ANX_PDC_anx74xx_UCSI_CTRL_REG_VER              0x00
#define ANX_PDC_anx74xx_UCSI_CTRL_REG_CCI_VAL0         0x00
#define ANX_PDC_anx74xx_UCSI_CTRL_REG_CCI_VAL1         0x04
#define ANX_PDC_anx74xx_UCSI_CTRL_REG_CMD              0x08
#define ANX_PDC_anx74xx_UCSI_CTRL_REG_DL               0x09
#define ANX_PDC_anx74xx_UCSI_CTRL_REG_SPD              0x0a

#define ANX_PDC_anx74xx_UCSI_CTRL_REG_UCSI_FLAG_P0             0x10
#define ANX_PDC_anx74xx_UCSI_CTRL_REG_UCSI_FLAG_P1             0x11
#define ANX_PDC_anx74xx_UCSI_CTRL_UCSI_CCI_EVT                  BIT(0)  
#define ANX_PDC_anx74xx_UCSI_CTRL_UCSI_CCI_ALERT                BIT(1)  
#define ANX_PDC_anx74xx_UCSI_CTRL_UCSI_FLAG_INIT_DONE           BIT(7)

#define ANX_PDC_anx74xx_UCSI_CTRL_UCSI_OCM_DEBUG      0x12

#define ANX_PDC_anx74xx_UCSI_PORT0_OPTIONAL_FEATURE_B0           0x13
#define ANX_PDC_anx74xx_UCSI_PORT0_OPTIONAL_FEATURE_B1           0x14
#define ANX_PDC_anx74xx_UCSI_PORT0_OPTIONAL_FEATURE_B2           0x15
#define ANX_PDC_anx74xx_UCSI_PORT1_OPTIONAL_FEATURE_B0           0x16
#define ANX_PDC_anx74xx_UCSI_PORT1_OPTIONAL_FEATURE_B1           0x17
#define ANX_PDC_anx74xx_UCSI_PORT1_OPTIONAL_FEATURE_B2           0x18
#define ANX_PDC_anx74xx_UCSI_OPTIONAL_FEATURE_VAL                0xa37


#define ANX_PDC_anx74xx_UCSI_CTRL_NTF_EN0             0x19

// #define ANX_PDC_anx74xx_UCSI_CTRL_REG_MUX_CTRL         0x1f
// #define ANX_PDC_anx74xx_UCSI_CTRL_REG_MUX_CTRL_P0_ACC         BIT(1)
// #define ANX_PDC_anx74xx_UCSI_CTRL_REG_MUX_CTRL_P1_ACC         BIT(2)
// #define ANX_PDC_anx74xx_UCSI_CTRL_REG_MUX_CTRL_BUSY_ACC_MASK  (BIT(1)|BIT(2))

#define ANX_PDC_anx74xx_UCSI_MSG_IN_REG_REGON0       0x00
#define ANX_PDC_anx74xx_UCSI_MSG_IN_REG_REGON1       0x80
#define ANX_PDC_anx74xx_UCSI_MSG_OUT_REG_REGON0      0x00
#define ANX_PDC_anx74xx_UCSI_MSG_OUT_REG_REGON1      0x80

#define ANX_PDC_anx74xx_INFO_REG_FW_VERSION          0xF8
#define ANX_PDC_anx74xx_INFO_REG_CSTM_VERSION        0xFB

#define ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL       0xFC 

#define ANX_PDC_anx74xx_PCH_REG_DATA_STATUS        0x5f

/* bit positions */
#define ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_HIGH_VOL_FOR_SAME_POWER    BIT(7)
#define ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_DP_PREFER_DFP_U	        BIT(6)   //reject DR_swap on DFP (Prefer_DFU_U DP alter 2.0)
#define ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_DP_PREFER_UFP_U	        BIT(5)   //reject DR_swap on UFP (Prefer_UFU_U DP alter 2.0)
#define ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_SUPPORT_GOTO_MIN_POWER     BIT(4)
#define ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_TRY_SNK_EN                 BIT(3)
#define ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_TRY_SRC_EN                 BIT(2)
#define ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_VPRO_SUPPORT              	BIT(1)   
#define ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_CC_SBU_SWAP              	BIT(0)

#define ANX_PDC_anx74xx_ADDRESS_COUNT     8 
#define ANX_PDC_anx74xx_ADDRESS_MAP_COUNT 6
#define ANX_PDC_anx74xx_ADDRESS_IND_TCPC         0
#define ANX_PDC_anx74xx_ADDRESS_IND_TOP          1
#define ANX_PDC_anx74xx_ADDRESS_IND_UCSI_MSG_IN  2
#define ANX_PDC_anx74xx_ADDRESS_IND_UCSI_MSG_OUT 3
#define ANX_PDC_anx74xx_ADDRESS_IND_UCSI_CTRL    4
#define ANX_PDC_anx74xx_ADDRESS_IND_INFO         5
#define ANX_PDC_anx74xx_ADDRESS_IND_EMTB         6
#define ANX_PDC_anx74xx_ADDRESS_IND_PCH          7

const uint8_t ANX_PDC_anx74xx_ADDRESS_MAP[ANX_PDC_anx74xx_ADDRESS_MAP_COUNT][ANX_PDC_anx74xx_ADDRESS_COUNT] = {
    // ctrl, intr, msg in, msg out, ucsi ctrl, info, emtb, pch
    // {0x58, 0x7e, 0x30, 0x32, 0x34, 0x58, 0x7a, 0x72 }, // 8bit display
    // {0x56, 0x6e, 0x36, 0x38, 0x3a, 0x56, 0x78, 0x76 }, // 8bit display
    // {0x54, 0x64, 0x3c, 0x3e, 0x28, 0x54, 0x68, 0x5c }, // 8bit display
    // {0x10, 0x7e, 0x30, 0x32, 0x34, 0x58, 0x12, 0xc0 }, // 8bit display
    // {0x16, 0x6e, 0x36, 0x38, 0x3a, 0x56, 0x18, 0xc2 }, // 8bit display
    // {0x20, 0x64, 0x3c, 0x3e, 0x28, 0x54, 0x22, 0xc4 }, // 8bit display
    {0x2c, 0x3f, 0x18, 0x19, 0x1a, 0x2c, 0x3d, 0x39 }, 
    {0x2b, 0x37, 0x1b, 0x1c, 0x1d, 0x2b, 0x3c, 0x3b }, 
    {0x2a, 0x32, 0x1e, 0x1f, 0x14, 0x2a, 0x34, 0x2e }, 
    {0x08, 0x3f, 0x18, 0x19, 0x1a, 0x2c, 0x09, 0x60 }, 
    {0x0b, 0x37, 0x1b, 0x1c, 0x1d, 0x2b, 0x0c, 0x61 }, 
    {0x10, 0x32, 0x1e, 0x1f, 0x14, 0x2a, 0x11, 0x62 }, 
};

/**
 * @brief Evt_cci Status of the PDC
 */
union cci_status_t {
    struct {
        /** Command event cci*/
        uint8_t cmd_evt_cci : 1;
        /** Reserve */
        uint8_t reserve : 7;
    };
    uint8_t raw_value;
};

/**
 * @brief States of the main state machine
 */
enum state_t {
    /** Init State */
    ST_INIT,
    /** Idle State */
    ST_IDLE,
    /** Write State */
    ST_WRITE,
    /** CCI State */
    ST_CCI_STATUS,
    /** Read State */
    ST_READ,
    /** Error Recovery State */
    ST_ERROR_RECOVERY,
    /** Disable State */
    ST_DISABLE,
    /** PDC communication suspended */
    ST_SUSPENDED,
};

/**
 * @brief Init sub-states
 */
enum init_state_t {
    /** Enable the PDC */
    INIT_PDC_WAKEUP,
    /** Get the PDC IC Status */
    INIT_PDC_GET_IC_STATUS,
    /** Reset the PDC UCSI bypass status machine*/
    INIT_PDC_PPM_RESET,
    /** Set the PDC Notifications */
    INIT_PDC_SET_NOTIFICATION_ENABLE,
    /** Initialization complete */
    INIT_PDC_COMPLETE,
    /** Initialization error */
    INIT_ERROR,
    /** Wait for command to send */
    INIT_PDC_CMD_WAIT
};

/**
 * @brief PDC Config object
 */
struct pdc_config_t {
    /** I2C config */
    struct i2c_dt_spec i2c;
    /** pdc power path interrupt */
    struct gpio_dt_spec irq_gpios;
    /** connector number of this port */
    uint8_t connector_number;
    /** Notification enable bits */
    union notification_enable_t bits;
    /** Create thread function */
    void (*create_thread)(const struct device *dev);
    /** If true, do not apply PDC FW updates to this port */
    bool no_fw_update;
	/** Whether or not this port supports CCD */
	bool ccd;
	/** Whether or not this port supports FRS */
	bool frs_supported;
	/** Pointer to the device-specific callback function */
	gpio_callback_handler_t callback_handler;
};

/**
 * @brief PDC Data object
 */
struct pdc_data_t {
    /** State machine context */
    struct smf_ctx ctx;
    /** Init's local state variable */
    enum init_state_t init_local_state;
    /** Init's current state */
    //enum init_state_t init_local_current_state;
    /** Init's next state */
    enum init_state_t init_local_next_state;
    /** PDC's last state */
    enum state_t last_state;
    /** PDC device structure */
    const struct device *dev;
    /** Driver thread */
    k_tid_t thread;
    /** Driver thread's data */
    struct k_thread thread_data;
    /** Timepoint for when we can next call cci status. */
    k_timepoint_t next_cci_status;
    /** Number of time the init process has been attempted */
    uint8_t init_retry_counter;
    /** I2C retry counter */
    uint8_t i2c_transaction_retry_counter;
    /** PDC i2c address map */
    uint8_t i2c_address_map[ANX_PDC_anx74xx_ADDRESS_COUNT];// 0 tcpc // 1 top intr // 2 UCSI msg in 1 // 3 UCSI msg out // 4 UCSI ctrl // 5 info
    /** Pointer to user data */
    uint8_t *user_buf;
    uint8_t user_buf_len;
    /** Command mutex */
    struct k_mutex mtx;
    /** GPIO interrupt callback */
    struct gpio_callback gpio_cb;
    /** Error status */
    union error_status_t error_status;
    /** CCI Event */
    union cci_event_t cci_event;
    /** CC Event callback */
    struct pdc_callback *cc_cb;
    /** CC Event one-time callback. If it's NULL, cci_cb will be called. */
    struct pdc_callback *cc_cb_tmp;
    /** Asynchronous (CI) Event callbacks */
    sys_slist_t ci_cb_list;
    /** Information about the PDC */
    struct pdc_info_t info;
    /** Init done flag */
    bool init_done;
    /** Error recovery delay counter */
    uint16_t error_recovery_delay_counter;
    /** Error recovery counter */
    uint16_t error_recovery_counter;
    /** Error Status used during initialization */
    union error_status_t es;
    /* Driver specific events to handle. */
    struct k_event driver_event;
    /* Currently running UCSI command. */
    enum ucsi_command_t active_ucsi_cmd;
////////////////////////////////////////////////
    struct ucsi_memory_region internal_ucsi_region;
    bool internal_ucsi_region_p1;
    uint32_t rdo_as_sink;
    bool direct_return_cci;
};

/**
 * @brief List of human readable state names for console debugging
 */
/* TODO(b/325128262): Bug to explore simplifying the the state machine */
static const char *const state_names[] = {
    [ST_INIT] = "INIT",
    [ST_IDLE] = "IDLE",
    [ST_WRITE] = "WRITE",
    [ST_CCI_STATUS] = "CCI_STATUS",
    [ST_READ] = "READ",
    [ST_ERROR_RECOVERY] = "ERROR_RECOVERY",
    [ST_DISABLE] = "PDC_DISABLED",
    [ST_SUSPENDED] = "PDC_SUSPENDED",
};

static const struct smf_state states[];
static int anx74xx_wakeup(const struct device *dev);
static int anx74xx_ppm_reset(const struct device *dev);
static int anx74xx_set_notification_enable(const struct device *dev,
                     union notification_enable_t bits,
                     uint16_t ext_bits);
static int anx74xx_ack_cc_ci(const struct device *dev,
               union conn_status_change_bits_t ci, bool cc,
               uint16_t vendor_defined);
static int anx74xx_get_info(const struct device *dev, struct pdc_info_t *info,
              bool live);
static int anx74xx_get_error_status(const struct device *dev,
                  union error_status_t *es);

static int anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(const struct device *dev,
    struct ucsi_control_t ucsi_ctrl,
    uint8_t* msg_out, 
    uint8_t *user_buf,
    uint8_t user_buf_len,
    struct pdc_callback *callback);

/**
 * @brief PDC port data used in interrupt handler
 */
static struct pdc_data_t *pdc_data[CONFIG_USB_PD_PORT_MAX_COUNT];

/**
 * @brief Pointer to thread specific k_event that handles interrupts.
 */
static struct k_event *irq_event[CONFIG_USB_PD_PORT_MAX_COUNT];

static enum state_t get_state(struct pdc_data_t *data)
{
    return data->ctx.current - &states[0];
}

static void set_state(struct pdc_data_t *data, const enum state_t next_state)
{
    data->last_state = get_state(data);
    smf_set_state(SMF_CTX(data), &states[next_state]);
    k_event_post(&data->driver_event, anx74xx_NEXT_STATE_READY);
}

/**
 * Atomic flag to suspend sending new commands to chip
 *
 * This flag is shared across driver instances.
 *
 * TODO(b/323371550) When more than one PDC is supported, this flag will need
 * to be tracked per-chip.
 */
static atomic_t suspend_comms_flag = ATOMIC_INIT(0);

static void suspend_comms(void)
{
    atomic_set(&suspend_comms_flag, 1);
}

static void enable_comms(void)
{
    atomic_set(&suspend_comms_flag, 0);
}

static bool check_comms_suspended(void)
{
    return atomic_get(&suspend_comms_flag) != 0;
}

static void print_current_state(struct pdc_data_t *data)
{
    ANX_DEBUG_PRINT_HEAD
    int st = get_state(data);

    if (st == ST_ERROR_RECOVERY) {
        ANX_DEBUG("ST%d: %s %d", cfg->connector_number,state_names[st],data->error_recovery_counter);
    } else {
        ANX_DEBUG("ST%d: %s", cfg->connector_number, state_names[st]);
    }
}

static void call_cci_event_cb(struct pdc_data_t *data)
{
    ANX_DEBUG_PRINT_HEAD
    union cci_event_t cci = data->cci_event;

    if (!data->init_done) {
        return;
    }

    if(cci.raw_value==0x20000000)
    {
        cci.raw_value=0x80000000;
    }

    ANX_DEBUG("AnxPort%d: CCI=0x%x", cfg->connector_number, cci.raw_value);

    /*
     * CC and CI events are separately reported. So, we need to call only
     * one callback or the other.
     */
    if (cci.connector_change) {
        pdc_fire_callbacks(&data->ci_cb_list, data->dev, cci);
    } else if (data->cc_cb_tmp) {
        data->cc_cb_tmp->handler(data->dev, data->cc_cb_tmp, cci);
    } else if (data->cc_cb) {
        data->cc_cb->handler(data->dev, data->cc_cb, cci);
    }

    data->cci_event.raw_value = 0;
}

static void perform_pdc_init(struct pdc_data_t *data)
{
    data->init_retry_counter = 0;
    data->error_status.raw_value = 0;
    /* Set initial local state of Init */
    data->init_local_state = INIT_PDC_WAKEUP;
    set_state(data, ST_INIT);
}

/**
 * @brief This function should be called after any I2C transfer that failed.
 * It increments a counter, notifies the subsystem of the I2C error and then
 * enters the recovery state. NOTE: the data->i2c_transaction_retry_counter
 * should be set to zero in the calling states entry action.
 */
static bool max_i2c_retry_reached(struct pdc_data_t *data, int type)
{
    const struct pdc_config_t *cfg = data->dev->config;

    data->i2c_transaction_retry_counter++;
    if (data->i2c_transaction_retry_counter > N_I2C_TRANSACTION_COUNT) {
        /* MAX I2C transactions exceeded */
        LOG_ERR("AnxPort%d: %s i2c error", cfg->connector_number,
            (type & I2C_MSG_READ) ? "Read" : "Write");
        /*
         * The command was not successfully completed,
         * so set cci.error to 1b.
         */
        data->cci_event.error = 1;
        /* Command has completed */
        data->cci_event.command_completed = 1;
        /* Clear busy event */
        data->cci_event.busy = 0;
        /* Set error, I2C read error */
        if (type & I2C_MSG_READ) {
            data->error_status.i2c_read_error = 1;
        } else {
            data->error_status.i2c_write_error = 1;
        }
        /* Notify system of status change */
        call_cci_event_cb(data);
        return true;
    }
    return false;
}

static int anx74xx_i2c_read(const struct device *dev, uint8_t addr, uint8_t offset, uint8_t length, uint8_t* buf)
{
    const struct pdc_config_t *cfg = dev->config;
    struct i2c_msg msg[2];

    msg[0].buf = &offset;
    msg[0].len = 1;
    msg[0].flags = I2C_MSG_WRITE;

    msg[1].buf = buf;
    msg[1].len = length;
    msg[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;
    int rv = i2c_transfer(cfg->i2c.bus, msg, 2, addr);
    if(rv<0)
    {
        LOG_ERR("AnxPort%d: Read i2c error", cfg->connector_number);
    }
    //ANX_DEBUG("AnxPort%d: anx74xx_i2c_read addr 0x%x, offset 0x%x, rv %x", cfg->connector_number, addr, offset, rv);
    return rv;
}

static int anx74xx_i2c_write(const struct device *dev, uint8_t addr, uint8_t offset, uint8_t length, uint8_t* buf)
{
    const struct pdc_config_t *cfg = dev->config;
    struct i2c_msg msg;
    uint8_t buffer[256];
    buffer[0] = offset;
    length = length<255?length:255;
    memcpy(&buffer[1], buf, length);

    msg.buf = buffer;
    msg.len = length+1;
    msg.flags = I2C_MSG_WRITE | I2C_MSG_STOP;
    int rv = i2c_transfer(cfg->i2c.bus, &msg, 1, addr);
    if(rv<0)
    {
        LOG_ERR("AnxPort%d: Write i2c error", cfg->connector_number);
    }
    //ANX_DEBUG("AnxPort%d: anx74xx_i2c_write addr 0x%x, offset 0x%x, rv %x", cfg->connector_number, addr, offset, rv);
    return rv;
}

static int anx74xx_on_init_done(const struct device *dev)
{
    struct pdc_data_t *data = dev->data;
    uint16_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_UCSI_CTRL];
    uint8_t wval;
    int rv;

    if(data->internal_ucsi_region_p1) //port 1
    {
        rv = anx74xx_i2c_read(dev,addr,ANX_PDC_anx74xx_UCSI_CTRL_REG_UCSI_FLAG_P0,1,&wval);
        if(rv<0)
        {
            return rv;
        }
        if(!(wval&ANX_PDC_anx74xx_UCSI_CTRL_UCSI_FLAG_INIT_DONE)) // wait internal P0 init done
        {
            return -EBUSY;
        }
        uint16_t optionalf = ANX_PDC_anx74xx_UCSI_OPTIONAL_FEATURE_VAL;
        rv = anx74xx_i2c_write(dev,addr,ANX_PDC_anx74xx_UCSI_PORT1_OPTIONAL_FEATURE_B0,2,(uint8_t*)&optionalf);
        if(rv<0)
        {
            return rv;
        }
        return 0;
    }
    else //port 0
    {
        wval = ANX_PDC_anx74xx_UCSI_CTRL_UCSI_FLAG_INIT_DONE;
        rv = anx74xx_i2c_write(dev,addr,ANX_PDC_anx74xx_UCSI_CTRL_REG_UCSI_FLAG_P0,1,&wval);
        if(rv<0)
        {
            return rv;
        }
        uint16_t optionalf = ANX_PDC_anx74xx_UCSI_OPTIONAL_FEATURE_VAL;
        rv = anx74xx_i2c_write(dev,addr,ANX_PDC_anx74xx_UCSI_PORT0_OPTIONAL_FEATURE_B0,2,(uint8_t*)&optionalf);
        if(rv<0)
        {
            return rv;
        }
    }
    return 0;
}

static int anx74xx_ucsi_bypass_read_msg_in(const struct device *dev)
{
    struct pdc_data_t *data = dev->data;

    uint16_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_UCSI_MSG_IN];
    uint8_t reg = (data->internal_ucsi_region_p1)?ANX_PDC_anx74xx_UCSI_MSG_IN_REG_REGON1:ANX_PDC_anx74xx_UCSI_MSG_IN_REG_REGON0;
    return anx74xx_i2c_read(dev, addr, reg, data->internal_ucsi_region.cci.data_len, data->internal_ucsi_region.message_in);
}

static int anx74xx_ucsi_bypass_cmd_write(const struct device *dev)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    int rv = 0;
    uint8_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_UCSI_MSG_OUT];
    uint8_t reg = (data->internal_ucsi_region_p1)?ANX_PDC_anx74xx_UCSI_MSG_OUT_REG_REGON1:ANX_PDC_anx74xx_UCSI_MSG_OUT_REG_REGON0;

    ANX_DEBUG("AnxPort%d: anx74xx_ucsi_bypass_cmd_write addr %x, cmd 0x%x, len %x, ds %02x %02x %02x %02x %02x %02x", cfg->connector_number, addr, 
                    data->internal_ucsi_region.control.command,
                    data->internal_ucsi_region.control.data_length,
                    data->internal_ucsi_region.control.command_specific[0],
                    data->internal_ucsi_region.control.command_specific[1],
                    data->internal_ucsi_region.control.command_specific[2],
                    data->internal_ucsi_region.control.command_specific[3],
                    data->internal_ucsi_region.control.command_specific[4],
                    data->internal_ucsi_region.control.command_specific[5]);

    if(data->internal_ucsi_region.control.data_length>0){

        rv = anx74xx_i2c_write(dev, addr, reg+8, data->internal_ucsi_region.control.data_length,data->internal_ucsi_region.message_out);
        if(rv<0){
            return rv;
        }
    }
    rv = anx74xx_i2c_write(dev, addr, reg+1, 7, &data->internal_ucsi_region.control.data_length);
    if(rv<0){
        return rv;
    }
    rv = anx74xx_i2c_write(dev, addr, reg, 1, &data->internal_ucsi_region.control.command);
    if(rv<0){
        return rv;
    }

    data->active_ucsi_cmd = data->internal_ucsi_region.control.command;
    data->internal_ucsi_region.control.command = 0;
    return rv;
}

static int get_cci_status(const struct device *dev)
{
    struct pdc_data_t *data = dev->data;
    uint8_t cci_evt_status;
    uint8_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_UCSI_CTRL];
    uint8_t reg_cci_evt = (data->internal_ucsi_region_p1)?ANX_PDC_anx74xx_UCSI_CTRL_REG_UCSI_FLAG_P1:ANX_PDC_anx74xx_UCSI_CTRL_REG_UCSI_FLAG_P0;
    int rv = anx74xx_i2c_read(dev,addr,reg_cci_evt,1,&cci_evt_status);
    if(rv<0)
    {
        return rv;
    }
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: get_cci_status %x", cfg->connector_number, cci_evt_status);
    if(cci_evt_status&ANX_PDC_anx74xx_UCSI_CTRL_UCSI_CCI_EVT)
    {
        uint8_t reg_cci_val = (data->internal_ucsi_region_p1)?ANX_PDC_anx74xx_UCSI_CTRL_REG_CCI_VAL1:ANX_PDC_anx74xx_UCSI_CTRL_REG_CCI_VAL0;
        rv = anx74xx_i2c_read(dev,addr,reg_cci_val,4,(uint8_t*)&data->internal_ucsi_region.cci.raw_value);
        if(rv<0)
        {
            return rv;
        }
        //TODO  read msg in
        cci_evt_status &= ~ANX_PDC_anx74xx_UCSI_CTRL_UCSI_CCI_EVT;
        rv = anx74xx_i2c_write(dev,addr,reg_cci_evt,1,&cci_evt_status);
        if(rv<0)
        {
            return rv;
        }
    }
    else{
        return -EBUSY;
    }
    return rv;
}

// static int get_cci_status(const struct device *dev)
// {
//     struct pdc_data_t *data = dev->data;
//     union cci_status_t status;
//     uint8_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_UCSI_CTRL];
//     int rv = anx74xx_i2c_read(dev,addr,ANX_PDC_anx74xx_UCSI_CTRL_REG_EVT_CTRL,1,&status.raw_value);
//     if(rv<0)
//     {
//         return rv;
//     }
//     //const struct pdc_config_t *cfg = dev->config;
//     //ANX_DEBUG("AnxPort%d: get_cci_status %x", cfg->connector_number, status.cmd_evt_cci);
//     if(status.cmd_evt_cci)
//     {
//         rv = anx74xx_i2c_read(dev,addr,ANX_PDC_anx74xx_UCSI_CTRL_REG_CCI,4,(uint8_t*)&data->internal_ucsi_region.cci.raw_value);
//         if(rv<0)
//         {
//             return rv;
//         }
//         status.cmd_evt_cci = 0;
//         rv = anx74xx_i2c_write(dev,addr,ANX_PDC_anx74xx_UCSI_CTRL_REG_EVT_CTRL,1,&status.raw_value);
//         if(rv<0)
//         {
//             return rv;
//         }
//         addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_TOP];
//         uint8_t val=0x80; 
//         rv = anx74xx_i2c_write(dev,addr,ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE,1,&val);
//         if(rv<0)
//         {
//             return rv;
//         }
//     }
//     else{
//         rv = -EBUSY;
//     }
//     return rv;
// }

// static void init_write_cmd_and_change_state(struct pdc_data_t *data,
//                         enum init_state_t next)
// {
//     data->init_local_current_state = data->init_local_state;
//     data->init_local_next_state = next;
//     data->init_local_state = INIT_PDC_CMD_WAIT;
//     set_state(data, ST_WRITE);
// }

// static void init_display_error_status(struct pdc_data_t *data)
// {
//     const struct pdc_config_t *cfg = data->dev->config;
//     int cnum = cfg->connector_number;

//     if (data->es.unrecognized_command) {
//         LOG_ERR("AnxPort%d: Unrecognized Command", cnum);
//     }

//     if (data->es.non_existent_connector_number) {
//         LOG_ERR("AnxPort%d: Invalid Connector Number", cnum);
//     }

//     if (data->es.invalid_command_specific_param) {
//         LOG_ERR("AnxPort%d: Invalid Param", cnum);
//     }

//     if (data->es.incompatible_connector_partner) {
//         LOG_ERR("AnxPort%d: Invalid Connector Partner", cnum);
//     }

//     if (data->es.cc_communication_error) {
//         LOG_ERR("C:%d CC Comm Error", cnum);
//     }

//     if (data->es.cmd_unsuccessful_dead_batt) {
//         LOG_ERR("C:%d Dead Batt Error", cnum);
//     }

//     if (data->es.contract_negotiation_failed) {
//         LOG_ERR("C:%d Contract Negotiation Failed", cnum);
//     }
// }

static void st_init_entry(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;

    print_current_state(data);
    data->i2c_transaction_retry_counter = 0;
    data->init_done = false;
    /* pdc_init_failed is cleared when init process is complete */
    data->es.pdc_init_failed = 1;
}

static enum smf_state_result st_init_run(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;
    const struct pdc_config_t *cfg = data->dev->config;
    int cnum = cfg->connector_number;
    int rv;
    
    /* Do not start executing commands if suspended */
    if (check_comms_suspended()) {
        set_state(data, ST_SUSPENDED);
        return SMF_EVENT_HANDLED;
    }

    switch (data->init_local_state) {
    case INIT_PDC_WAKEUP:
        rv = anx74xx_wakeup(data->dev);
        if (rv < 0) {
            data->i2c_transaction_retry_counter++;
            if (data->i2c_transaction_retry_counter > N_I2C_TRANSACTION_COUNT) {
                LOG_ERR("C:%d, Internal(INIT_PDC_WAKEUP) cannot wake up chip", cnum);
                set_state(data, ST_DISABLE);
            }
            return SMF_EVENT_HANDLED;
        }
        data->init_local_state = INIT_PDC_GET_IC_STATUS;
        k_event_post(&data->driver_event, anx74xx_NEXT_STATE_READY); // to trigger thread continue processing
        return SMF_EVENT_HANDLED;
    case INIT_PDC_GET_IC_STATUS:
        rv = anx74xx_get_info(data->dev, &data->info, true);
        if (rv) {
            LOG_ERR("C:%d, Internal(INIT_PDC_GET_IC_STATUS)", cnum);
            set_state(data, ST_DISABLE);
            return SMF_EVENT_HANDLED;
        }
        if(!data->internal_ucsi_region_p1)// chip init process only need by one thread
        {
            data->init_local_state = INIT_PDC_PPM_RESET;
        }
        else
        {
            data->init_local_state = INIT_PDC_COMPLETE;
        }
        k_event_post(&data->driver_event, anx74xx_NEXT_STATE_READY); // to trigger thread continue processing
        return SMF_EVENT_HANDLED;
    case INIT_PDC_PPM_RESET:
        rv = anx74xx_ppm_reset(data->dev);
        if (rv) {
            LOG_ERR("C:%d, Internal(INIT_PDC_PPM_RESET)", cnum);
            set_state(data, ST_DISABLE);
            return SMF_EVENT_HANDLED;
        }
        data->init_local_state = INIT_PDC_CMD_WAIT;
        data->init_local_next_state = INIT_PDC_SET_NOTIFICATION_ENABLE;
        set_state(data, ST_WRITE);
        return SMF_EVENT_HANDLED;
    case INIT_PDC_SET_NOTIFICATION_ENABLE:
        rv = anx74xx_set_notification_enable(data->dev, cfg->bits, 0x0);
        if (rv) {
            LOG_ERR("C:%d, Internal(INIT_PDC_SET_NOTIFICATION_ENABLE)",
                cnum);
            set_state(data, ST_DISABLE);
            return SMF_EVENT_HANDLED;
        }
        data->init_local_state = INIT_PDC_COMPLETE;
        k_event_post(&data->driver_event, anx74xx_NEXT_STATE_READY); // to trigger thread continue processing
        return SMF_EVENT_HANDLED;
    case INIT_PDC_COMPLETE:
        rv = anx74xx_on_init_done(data->dev);
        if (rv== -EBUSY) {
            return SMF_EVENT_HANDLED;
        }
        else if (rv) {
            ANX_DEBUG("AnxPort%d: st_init_run init_err %d", cfg->connector_number, -EBUSY);
            set_state(data, ST_DISABLE);
            return SMF_EVENT_HANDLED;
        }
        else {
            data->es.pdc_init_failed = 0;
            /* Init is complete, so transition to Idle state */
            set_state(data, ST_IDLE);
            data->init_done = true;
            ANX_DEBUG("AnxPort%d: st_init_run init_done", cfg->connector_number);
        }
        return SMF_EVENT_HANDLED;
    case INIT_ERROR:
        /* Get error status, and re-start the init process */
        anx74xx_get_error_status(data->dev, &data->es);
        //init_write_cmd_and_change_state(data, INIT_PDC_WAKEUP);
        return SMF_EVENT_HANDLED;
    case INIT_PDC_CMD_WAIT:
        /* If PDC_RESET was sent, check the reset_completed flag */
        if (!data->cci_event.command_completed&&!data->cci_event.reset_completed) {
            return SMF_EVENT_HANDLED;
        }
        data->init_local_state = data->init_local_next_state;
        break;
    }
    return SMF_EVENT_HANDLED;
}

/**
 * @brief Called from the main thread to handle interrupts
 */
static int handle_irqs(struct pdc_data_t *data)
{
    const struct device *dev = data->dev;
    const struct pdc_config_t *cfg = dev->config;
    uint8_t top_addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_TOP];
    uint8_t intr = 0;
    uint8_t ucsi_addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_UCSI_CTRL];
    uint8_t evt_reg = (data->internal_ucsi_region_p1)?ANX_PDC_anx74xx_UCSI_CTRL_REG_UCSI_FLAG_P1:ANX_PDC_anx74xx_UCSI_CTRL_REG_UCSI_FLAG_P0;
    uint8_t val;
    int rv;
    if(data->init_done)
    {
        rv = anx74xx_i2c_read(dev,ucsi_addr,evt_reg,1,(uint8_t*)&val);
        //ANX_DEBUG("AnxPort%d: test %x %x %x", cfg->connector_number, ucsi_addr, evt_reg, val);
        if(rv<0)
        {
            return rv;
        }
        if(val&ANX_PDC_anx74xx_UCSI_CTRL_UCSI_CCI_ALERT)
        {
            val &= ~(ANX_PDC_anx74xx_UCSI_CTRL_UCSI_CCI_ALERT|ANX_PDC_anx74xx_UCSI_CTRL_UCSI_CCI_EVT);
            anx74xx_i2c_write(dev,ucsi_addr,evt_reg,1,&val);
            data->cci_event.raw_value = 0;
            data->cci_event.connector_change = (cfg->connector_number+1);
            data->cci_event.vendor_defined_indicator = 1;
            ANX_DEBUG("AnxPort%d: ucsi_intr %x", cfg->connector_number, data->cci_event.raw_value);
            call_cci_event_cb(data);
        }
    }
    rv = anx74xx_i2c_read(dev,top_addr,ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE,1,&intr);
    //ANX_DEBUG("AnxPort%d: handle_irqs %x", cfg->connector_number, intr);
    if(rv<0)
    {
        return rv;
    }
    if(intr&ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE_PROCHOT_INT) //one task clear interrupt bit is enough
    {
        ANX_DEBUG("AnxPort%d: prochot", cfg->connector_number);
    }
    if(intr&ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE_POW_ON)
    {
        ANX_DEBUG("AnxPort%d: power on", cfg->connector_number);
        data->init_done = false;
        data->init_local_state = ST_INIT;
        set_state(data, ST_INIT);
    }
    if(intr&ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE_POW_OFF)
    {
        ANX_DEBUG("AnxPort%d: power off", cfg->connector_number);
        // uint8_t tcpc_addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_TCPC];

        // val =ANX_PDC_anx74xx_TCPC_REG_TCPC_ROLE_CONTROL_VAL_DEFAULT;
        // anx74xx_i2c_write(dev,tcpc_addr,ANX_PDC_anx74xx_TCPC_REG_TCPC_ROLE_CONTROL,1,&val);
        // //set tcpc role as Rd + DRP en(0x4A)
        // val =ANX_PDC_anx74xx_TCPC_REG_TCPC_PD_DRP_VAL_DEFAULT;
        // anx74xx_i2c_write(dev,tcpc_addr,ANX_PDC_anx74xx_TCPC_REG_TCPC_PD_DRP,1,&val);// 85ms 50% duty

        // anx74xx_i2c_read(dev,tcpc_addr,ANX_PDC_anx74xx_TCPC_REG_TCPC_ANALOG_CTRL,1,&val);
        // val &=~(ANX_PDC_anx74xx_TCPC_REG_TCPC_ANALOG_CTRL_CABLE_DET_DIG|ANX_PDC_anx74xx_TCPC_REG_TCPC_ANALOG_CTRL_DIGITAL_RDY);
        // anx74xx_i2c_write(dev,tcpc_addr,ANX_PDC_anx74xx_TCPC_REG_TCPC_ANALOG_CTRL,1,&val);

        // /* DRP en */
        // val =ANX_PDC_anx74xx_TCPC_REG_TCPC_CMD_VAL_LOOK_FOR_CONNECT;
        // anx74xx_i2c_write(dev,tcpc_addr,ANX_PDC_anx74xx_TCPC_REG_TCPC_CMD,1,&val);// 85ms 50% duty

        // val =ANX_PDC_anx74xx_TCPC_REG_TCPC_CTRL3_VAL_DEFAULT;
        // anx74xx_i2c_write(dev,tcpc_addr,ANX_PDC_anx74xx_TCPC_REG_TCPC_CTRL3,1,&val);
        // val =ANX_PDC_anx74xx_TCPC_REG_TCPC_CTRL4_VAL_DEFAULT;
        // anx74xx_i2c_write(dev,tcpc_addr,ANX_PDC_anx74xx_TCPC_REG_TCPC_CTRL4,1,&val);

        // val =ANX_PDC_anx74xx_TCPC_REG_TCPC_CMD_VAL_DEEP_POWER_DOWN;
        // anx74xx_i2c_write(dev,tcpc_addr,ANX_PDC_anx74xx_TCPC_REG_TCPC_CMD,1,&val);
        // data->init_done = false;
        // data->init_local_state = ST_INIT;
        // set_state(data, ST_IDLE);
        // return 0;
    }
    if(!data->internal_ucsi_region_p1) //one task clear interrupt bit is enough
    {
        anx74xx_i2c_write(dev,top_addr,ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE,1,&intr);
        rv = anx74xx_i2c_read(dev,top_addr,ANX_PDC_anx74xx_TOP_REG_INTP_SOURCE,1,&intr);
        if(rv<0)
        {
            return rv;
        }
        if(intr!=0)
        {
            return -EBUSY;
        }
        rv = anx74xx_i2c_read(dev,ucsi_addr,evt_reg,1,(uint8_t*)&val);
        if(rv<0)
        {
            return rv;
        }
        if((val&ANX_PDC_anx74xx_UCSI_CTRL_UCSI_CCI_ALERT)!=0)
        {
            return -EBUSY;
        }
    }
    return 0;
}

static void st_idle_entry(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;

    print_current_state(data);

    data->active_ucsi_cmd = 0;
}

static enum smf_state_result st_idle_run(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;

    /* Do not start executing commands if suspended */
    if (check_comms_suspended()) {
        set_state(data, ST_SUSPENDED);
        return SMF_EVENT_HANDLED;
    }
    //const struct pdc_config_t *cfg = data->dev->config;
    //ANX_DEBUG("AnxPort%d: st_idle_run cmd %x", cfg->connector_number, data->internal_ucsi_region.control.command);

    if (data->internal_ucsi_region.control.command != 0) {
        set_state(data, ST_WRITE);
    }
    return SMF_EVENT_HANDLED;
}

static void st_write_entry(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;

    print_current_state(data);

    /* This state can only be entered from the Init and Idle states */
    assert(data->last_state == ST_INIT || data->last_state == ST_IDLE);

    /* Clear I2C transaction retry counter */
    data->i2c_transaction_retry_counter = 0;
}

static enum smf_state_result st_write_run(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;
    int rv;

    /* Write the command */
    rv = anx74xx_ucsi_bypass_cmd_write(data->dev);
    if (rv < 0) {
        if (max_i2c_retry_reached(data, I2C_MSG_WRITE)) {
            set_state(data, ST_ERROR_RECOVERY);
        }
        return SMF_EVENT_HANDLED;
    }

    /* I2C transaction succeeded. Set timepoint for next cci status. */
    data->next_cci_status = sys_timepoint_calc(K_MSEC(T_CCI_STATUS));
    set_state(data, ST_CCI_STATUS);
    return SMF_EVENT_HANDLED;
}

static void st_cci_status_entry(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;

    print_current_state(data);

    /* This state can only be entered from the Write, Init and Idle states */
    assert(data->last_state == ST_WRITE || data->last_state == ST_INIT || data->last_state == ST_IDLE);

    /* Clear I2c Transaction Retry Counter */
    data->i2c_transaction_retry_counter = 0;

}

static enum smf_state_result st_cci_status_run(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;
    ANX_DEBUG_PRINT_HEAD
    int rv;
    
    if(data->direct_return_cci)
    {
        data->cci_event.raw_value = data->internal_ucsi_region.cci.raw_value;
        ANX_DEBUG("AnxPort%d: st_cci_status_run data no need bypass %x", cfg->connector_number, data->cci_event.raw_value);
        data->direct_return_cci = 0;
        call_cci_event_cb(data);
        if (data->init_done) {
            set_state(data, ST_IDLE);
        } else {
            set_state(data, ST_INIT);
        }
        return SMF_EVENT_HANDLED;
    }
    
    /*
     * Make sure that we've waited sufficient time before re-reading cci
     * status. Otherwise PDC may be starved of time to execute commands.
     */
    // if (!sys_timepoint_expired(data->next_cci_status)) {
    //     k_sleep(sys_timepoint_timeout(data->next_cci_status));
    // }
    //ANX_DEBUG("AnxPort%d: st_cci_status_run", cfg->connector_number);
    /* Read the cci Status */
    rv = get_cci_status(data->dev);

     /* Reset time until next cci status. */
    data->next_cci_status = sys_timepoint_calc(K_MSEC(T_CCI_STATUS));

    if (rv < 0) {
        if(rv==-EBUSY){
            if(data->active_ucsi_cmd==0)
            {
                if (data->init_done) {
                    set_state(data, ST_IDLE);
                } else {
                    set_state(data, ST_INIT);
                }
            }
            return SMF_EVENT_HANDLED;
        }
        if (max_i2c_retry_reached(data, I2C_MSG_READ)) {
            set_state(data, ST_ERROR_RECOVERY);
        }
        return SMF_EVENT_HANDLED;
    }
    ANX_DEBUG("AnxPort%d: cmd %x, st_cci_status_run %x", cfg->connector_number, data->active_ucsi_cmd, data->internal_ucsi_region.cci.raw_value);
    if(data->internal_ucsi_region.cci.error)
    {
#if PRINT_CCI_ERROR
        data->cci_event.raw_value = data->internal_ucsi_region.cci.raw_value;
        struct ucsi_control_t ucsi_ctrl;
        memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
        ucsi_ctrl.command = UCSI_GET_ERROR_STATUS;
        ucsi_ctrl.data_length=0;
        ucsi_ctrl.command_specific[0] = (ANX_Test_Port_Number_Wraper+1)&0x7f;
        anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(data->dev, ucsi_ctrl, NULL,NULL,0,NULL);
        set_state(data, ST_WRITE);
        return SMF_EVENT_HANDLED;
#else
        data->internal_ucsi_region.cci.raw_value = 0;
        data->internal_ucsi_region.cci.command_completed = 1;
#endif
    }
    if(data->active_ucsi_cmd==UCSI_GET_ERROR_STATUS)
    {
        anx74xx_ucsi_bypass_read_msg_in(data->dev);
        ANX_DEBUG("AnxPort%d: Error Reason %04x", cfg->connector_number, *(uint16_t*)data->internal_ucsi_region.message_in);
        data->cci_event.raw_value = 0;
        data->cci_event.command_completed = 1;
        call_cci_event_cb(data);
    }
    else if (data->active_ucsi_cmd > 0)
    {
        if(data->internal_ucsi_region.cci.data_len>0)
        {
            set_state(data, ST_READ);
            return SMF_EVENT_HANDLED;
        }
        data->cci_event.raw_value = data->internal_ucsi_region.cci.raw_value;
        call_cci_event_cb(data);
    }
    if(data->init_done)
    {
        set_state(data, ST_IDLE);
    }
    else
    {
        set_state(data, ST_INIT);
    }
    return SMF_EVENT_HANDLED;
}

static void st_read_entry(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;

    print_current_state(data);

    /* This state can only be entered from the cci Status state */
    assert(data->last_state == ST_CCI_STATUS);

    /* Clear I2c Transaction Retry Counter */
    data->i2c_transaction_retry_counter = 0;
    /* Set the port the CCI Event occurred on */
}

static enum smf_state_result st_read_run(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;
    const struct pdc_config_t *cfg = data->dev->config;
    uint8_t len;
    int rv;

    /*
     * The data->user_buf is checked for NULL before a command is queued.
     * The check here gauards against an eronious cci_status indicating
     * data is available for a command that doesn't send data.
     */
    if (!data->user_buf) {
        LOG_ERR("NULL read buffer pointer");
        /*
         * The command was not successfully completed,
         * so set cci.error to 1b.
         */
        data->cci_event.error = 1;
        /* Command completed */
        data->cci_event.command_completed = 1;
        /* Null buffer error */
        data->error_status.null_buffer_error = 1;

        union conn_status_change_bits_t cc;
        cc.raw_value = 0;
        //anx74xx_ack_cc_ci(data->dev,cc,true,0);
        //set_state(data, ST_WRITE);
        call_cci_event_cb(data);
        set_state(data, ST_IDLE);
        return SMF_EVENT_HANDLED;
    }

    rv = anx74xx_ucsi_bypass_read_msg_in(data->dev);
    if (rv < 0) {
        if (max_i2c_retry_reached(data, I2C_MSG_READ)) {
            set_state(data, ST_ERROR_RECOVERY);
        }
        return SMF_EVENT_HANDLED;
    }

    len = data->internal_ucsi_region.cci.data_len;
    /* Copy the received data to the user's buffer */
    switch (data->active_ucsi_cmd) {
    //
    case UCSI_GET_CONNECTOR_STATUS:
        data->rdo_as_sink = *(uint32_t*)&data->internal_ucsi_region.message_in[4];
        // if(*(uint32_t*)&data->internal_ucsi_region.message_in[0] == 0x00006000)
        // {
        //     *(uint32_t*)&data->internal_ucsi_region.message_in[0] = 0x40036004;
        // }
        memcpy(data->user_buf, data->internal_ucsi_region.message_in, (len>data->user_buf_len)?data->user_buf_len:len);
        break;
    default:
        /* No preprocessing needed for the user data */
        memcpy(data->user_buf, data->internal_ucsi_region.message_in, (len>data->user_buf_len)?data->user_buf_len:len);
    }

	switch(len)
	{
		case 0:  ANX_DEBUG("C%d: st_read_run len %x", cfg->connector_number,len);break;
		case 1:  ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x", cfg->connector_number,len, data->user_buf[0]);break;
		case 2:  ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1]);break;
		case 3:  ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2]);break;
		case 4:  ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3]);break;
		case 5:  ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4]);break;
		case 6:  ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5]);break;
		case 7:  ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6]);break;
		case 8:  ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7]);break;
		case 9:  ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7],data->user_buf[8]);break;
		case 10: ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7],data->user_buf[8],data->user_buf[9]);break;
		case 11: ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7],data->user_buf[8],data->user_buf[9],data->user_buf[10]);break;
		case 12: ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7],data->user_buf[8],data->user_buf[9],data->user_buf[10],data->user_buf[11]);break;
		case 13: ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7],data->user_buf[8],data->user_buf[9],data->user_buf[10],data->user_buf[11],data->user_buf[12]);break;
		case 14: ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7],data->user_buf[8],data->user_buf[9],data->user_buf[10],data->user_buf[11],data->user_buf[12],data->user_buf[13]);break;
		case 15: ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7],data->user_buf[8],data->user_buf[9],data->user_buf[10],data->user_buf[11],data->user_buf[12],data->user_buf[13],data->user_buf[14]);break;
		case 16: ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7],data->user_buf[8],data->user_buf[9],data->user_buf[10],data->user_buf[11],data->user_buf[12],data->user_buf[13],data->user_buf[14],data->user_buf[15]);break;
		case 17: ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7],data->user_buf[8],data->user_buf[9],data->user_buf[10],data->user_buf[11],data->user_buf[12],data->user_buf[13],data->user_buf[14],data->user_buf[15],data->user_buf[16]);break;
		case 18: ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7],data->user_buf[8],data->user_buf[9],data->user_buf[10],data->user_buf[11],data->user_buf[12],data->user_buf[13],data->user_buf[14],data->user_buf[15],data->user_buf[16],data->user_buf[17]);break;
		case 19: ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7],data->user_buf[8],data->user_buf[9],data->user_buf[10],data->user_buf[11],data->user_buf[12],data->user_buf[13],data->user_buf[14],data->user_buf[15],data->user_buf[16],data->user_buf[17],data->user_buf[18]);break;
		case 20: ANX_DEBUG("C%d: st_read_run len %x, user_buf %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", cfg->connector_number,len, data->user_buf[0],data->user_buf[1],data->user_buf[2],data->user_buf[3],data->user_buf[4],data->user_buf[5],data->user_buf[6],data->user_buf[7],data->user_buf[8],data->user_buf[9],data->user_buf[10],data->user_buf[11],data->user_buf[12],data->user_buf[13],data->user_buf[14],data->user_buf[15],data->user_buf[16],data->user_buf[17],data->user_buf[18],data->user_buf[19]);break;
		default: LOG_ERR("C%d: st_read_run len %x, user_buf too long", cfg->connector_number,len);break;
	}
    /*
     * Set cci.data_len. This will be zero if no
     * data is available.
     */
    data->cci_event.data_len = len;
    /* Command has completed */
    data->cci_event.command_completed = 1;
    /* Inform the system of the event */
    //anx74xx_ucsi_bypass_cmd_write_release_i2c(data->dev);
    call_cci_event_cb(data);
    set_state(data, ST_IDLE);
    return SMF_EVENT_HANDLED;
}

static void st_error_recovery_entry(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;

    print_current_state(data);
    data->error_recovery_counter++;
    data->error_recovery_delay_counter = 0;

    /*TODO: ADD ERROR RECOVERY CODE */
}

static enum smf_state_result st_error_recovery_run(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;

    /* Don't continue trying if we are suspending communication */
    if (check_comms_suspended()) {
        set_state(data, ST_SUSPENDED);
        return SMF_EVENT_HANDLED;
    }

    if (data->error_recovery_counter >= N_MAX_ERROR_RECOVERY_COUNT) {
        set_state(data, ST_DISABLE);
        return SMF_EVENT_HANDLED;
    }

    /* Current recovery is just delaying and performing a PDC init */
    /* TODO(b/325633531): Investigate using timestamps instead of counters
     */
    if (data->error_recovery_delay_counter < N_ERROR_RECOVERY_DELAY_COUNT) {
        data->error_recovery_delay_counter++;
        return SMF_EVENT_HANDLED;
    }

    /* Perform PDC Init */
    perform_pdc_init(data);
    return SMF_EVENT_HANDLED;
}

static void st_disable_entry(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;

    print_current_state(data);
    /* If entering from ST_INIT state */
    data->init_done = true;
    data->error_status.port_disabled = 1;
}

static enum smf_state_result st_disable_run(void *o)
{
    /* Stay here until reset */
    return SMF_EVENT_HANDLED;
}

static void st_suspended_entry(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;

    print_current_state(data);
}

static enum smf_state_result st_suspended_run(void *o)
{
    struct pdc_data_t *data = (struct pdc_data_t *)o;

    /* Stay here while suspended */
    if (check_comms_suspended()) {
        return SMF_EVENT_HANDLED;
    }

    /* Otherwise, return back to init state...
     *
     * Start the driver initialization routine to put everything
     * back into a known state (This includes a driver + PDC reset)
     */
    perform_pdc_init(data);
    return SMF_EVENT_HANDLED;
}

/* Populate cmd state table */
static const struct smf_state states[] = {
    [ST_INIT] =
        SMF_CREATE_STATE(st_init_entry, st_init_run, NULL, NULL, NULL),
    [ST_IDLE] =
        SMF_CREATE_STATE(st_idle_entry, st_idle_run, NULL, NULL, NULL),
    [ST_WRITE] = SMF_CREATE_STATE(st_write_entry, st_write_run, NULL, NULL,
                      NULL),
    [ST_CCI_STATUS] = SMF_CREATE_STATE(
        st_cci_status_entry, st_cci_status_run, NULL, NULL, NULL),
    [ST_READ] =
        SMF_CREATE_STATE(st_read_entry, st_read_run, NULL, NULL, NULL),
    [ST_ERROR_RECOVERY] = SMF_CREATE_STATE(st_error_recovery_entry,
                           st_error_recovery_run, NULL,
                           NULL, NULL),
    [ST_DISABLE] = SMF_CREATE_STATE(st_disable_entry, st_disable_run, NULL,
                    NULL, NULL),
    [ST_SUSPENDED] = SMF_CREATE_STATE(st_suspended_entry, st_suspended_run,
                      NULL, NULL, NULL),

};

static int anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(const struct device *dev,
                        struct ucsi_control_t ucsi_ctrl,
                        uint8_t* msg_out, 
                        uint8_t *user_buf,
                        uint8_t user_buf_len,
                        struct pdc_callback *callback)
{
    //const struct pdc_config_t *cfg = dev->config;
    struct pdc_data_t *data = dev->data;
    /* Return an error if chip communication is suspended */
    if (check_comms_suspended()) {
        return -ECONNREFUSED;
    }

    k_mutex_lock(&data->mtx, K_FOREVER);

    if(msg_out)
    {
        assert(ucsi_ctrl.data_length <= ARRAY_SIZE(data->internal_ucsi_region.message_out));
        memcpy(data->internal_ucsi_region.message_out, msg_out, ucsi_ctrl.data_length);
    }
    memcpy(&data->internal_ucsi_region.control,&ucsi_ctrl,sizeof(struct ucsi_control_t));
    data->user_buf = user_buf;
    data->user_buf_len = user_buf_len;
    memset(&data->internal_ucsi_region.cci, 0, sizeof(union cci_event_t));
    memset(&data->cci_event, 0, sizeof(union cci_event_t));
    k_mutex_unlock(&data->mtx);
    /* Posting the event reduces latency to start executing the command. */
    k_event_post(&data->driver_event, anx74xx_NEXT_STATE_READY);
    data->direct_return_cci=false;
    return 0;
}

static int anx74xx_get_ucsi_version(const struct device *dev, uint16_t *version)
{
    if (version == NULL) {
        return -EINVAL;
    }
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_ucsi_version",cfg->connector_number);

    *version = UCSI_VERSION;

    return 0;
}

static int anx74xx_set_handler_cb(const struct device *dev,
                struct pdc_callback *callback)
{
    struct pdc_data_t *data = dev->data;
    //const struct pdc_config_t *cfg = dev->config;
    //ANX_DEBUG("AnxPort%d: anx74xx_set_handler_cb",cfg->connector_number);

    data->cc_cb = callback;

    return 0;
}

static int anx74xx_wakeup(const struct device *dev)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_wakeup",cfg->connector_number);

    /* Can only be called from Init State */
    if (get_state(data) != ST_INIT) {
        return -EBUSY;
    }
    
    uint16_t vid = 0;
    uint8_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_TCPC];
    int rv = anx74xx_i2c_read(dev,addr,ANX_PDC_anx74xx_INFO_REG_VID,2,(uint8_t*)&vid);
    if(rv<0){
        return -EACCES;
    }
    else if  (ANX_PDC_anx74xx_INFO_REG_VIDV!=vid){
        return -ENODEV;
    }
    vid = 0;
    addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_UCSI_CTRL];
    anx74xx_i2c_write(dev,addr,ANX_PDC_anx74xx_UCSI_CTRL_REG_UCSI_FLAG_P0,2,(uint8_t*)&vid); //clear all flags
    return 0;
}

static int anx74xx_set_retimer_update_mode(const struct device *dev, bool enable)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_retimer_update_mode %x",cfg->connector_number, enable);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    return -ENOSYS;
}

static int anx74xx_read_power_level(const struct device *dev)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_read_power_level",cfg->connector_number);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_READ_POWER_LEVEL;
    ucsi_ctrl.data_length = 0;
    ucsi_ctrl.command_specific[0] = (ANX_Test_Port_Number_Wraper+1)&0x7f;
    ucsi_ctrl.command_specific[1] = 0x8f;
    ucsi_ctrl.command_specific[2] = 0x01;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,NULL,0,NULL);
}

static int anx74xx_reconnect(const struct device *dev)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_reconnect not support",cfg->connector_number);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    return -ENOSYS;
}

static int anx74xx_pdc_reset(const struct device *dev)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_pdc_reset",cfg->connector_number);

    if (get_state(data) == ST_DISABLE) {
        perform_pdc_init(data);
        return 0;
    }

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    uint8_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_TOP];
    uint8_t val = ANX_PDC_anx74xx_TOP_REG_FLASH_RAM_CTRL_RELOAD;
    anx74xx_i2c_write(dev, addr, ANX_PDC_anx74xx_UCSI_CTRL_REG_VER, 1, &val);

    data->direct_return_cci = true;
    data->internal_ucsi_region.cci.raw_value = 0;
    data->internal_ucsi_region.cci.command_completed = 1;
    set_state(data,ST_CCI_STATUS);

    return 0;
}

static int anx74xx_ppm_reset(const struct device *dev)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_ppm_reset",cfg->connector_number);

    if (get_state(data) != ST_INIT) {
        return -EBUSY;
    }

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_PPM_RESET;
    ucsi_ctrl.data_length = 0;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,NULL,0,NULL);
}

static int anx74xx_connector_reset(const struct device *dev,
                 union connector_reset_t reset)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_connector_reset %x",cfg->connector_number, reset.raw_value);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_CONNECTOR_RESET;
    ucsi_ctrl.data_length = 0;
    ucsi_ctrl.command_specific[0] = reset.raw_value;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,NULL,0,NULL);
}

static int anx74xx_set_power_level(const struct device *dev,
                 enum usb_typec_current_t tcc)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_power_level %d",cfg->connector_number, tcc);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }
    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_SET_POWER_LEVEL;
    ucsi_ctrl.data_length=0;
    ucsi_ctrl.command_specific[0] = ((ANX_Test_Port_Number_Wraper+1)&0x7f) | (1<<7);
    ucsi_ctrl.command_specific[1] = 0;
    ucsi_ctrl.command_specific[2] = tcc&0x07;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL, NULL,0,NULL);
}

static int anx74xx_set_sink_path(const struct device *dev, bool en)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_sink_path %d",cfg->connector_number,en);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_SET_SINK_PATH;
    ucsi_ctrl.data_length=0;
    ucsi_ctrl.command_specific[0] = ((ANX_Test_Port_Number_Wraper+1)&0x7f) | (en<<7);
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL, NULL,0,NULL);
}

static int anx74xx_set_notification_enable(const struct device *dev,
                     union notification_enable_t bits,
                     uint16_t ext_bits)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_notification_enable %x %x",cfg->connector_number, bits.raw_value, ext_bits);

    /* Can only be called from Init State */
    if (get_state(data) != ST_INIT) {
        return -EBUSY;
    }
    uint8_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_UCSI_CTRL];
    return anx74xx_i2c_write(dev, addr, ANX_PDC_anx74xx_UCSI_CTRL_NTF_EN0, 3, (uint8_t*)&bits.raw_value);
}

static int anx74xx_get_capability(const struct device *dev,
                struct capability_t *caps)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_capability",cfg->connector_number);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    if (caps == NULL) {
        return -EINVAL;
    }

    caps->bmAttributes.raw_value = 0x4547;
    caps->bmOptionalFeatures.raw_value = ANX_PDC_anx74xx_UCSI_OPTIONAL_FEATURE_VAL;
    caps->bNumConnectors = 2;
    caps->bcdBCVersion = 0x120;
    caps->bcdPDVersion = 0x300;
    caps->bcdUSBTypeCVersion = 0x200;

    data->direct_return_cci = true;
    data->internal_ucsi_region.cci.raw_value = 0;
    data->internal_ucsi_region.cci.command_completed = 1;
    set_state(data,ST_CCI_STATUS);

    return 0;
}

static int anx74xx_get_connector_capability(const struct device *dev,
                      union connector_capability_t *caps)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_connector_capability",cfg->connector_number);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    if (caps == NULL) {
        return -EINVAL;
    }

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_GET_CONNECTOR_CAPABILITY;
    ucsi_ctrl.data_length=0;
    ucsi_ctrl.command_specific[0] = (ANX_Test_Port_Number_Wraper+1)&0x7f;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,(uint8_t*)&caps->raw_value,sizeof(union connector_capability_t),NULL);
}

static int anx74xx_get_connector_status(const struct device *dev,
                      union connector_status_t *cs)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_connector_status",cfg->connector_number);
    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    if (cs == NULL) {
        return -EINVAL;
    }

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_GET_CONNECTOR_STATUS;
    ucsi_ctrl.data_length=0;
    ucsi_ctrl.command_specific[0] = (ANX_Test_Port_Number_Wraper+1)&0x7f;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,cs->raw_value,sizeof(union connector_status_t),NULL);
}

static int anx74xx_get_cable_property(const struct device *dev,
                    union cable_property_t *cp)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_cable_property",cfg->connector_number);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    if (cp == NULL) {
        return -EINVAL;
    }

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_GET_CABLE_PROPERTY;
    ucsi_ctrl.data_length=0;
    ucsi_ctrl.command_specific[0] = (ANX_Test_Port_Number_Wraper+1)&0x7f;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,(uint8_t*)cp->raw_value,sizeof(union cable_property_t),NULL);
}

static int anx74xx_get_error_status(const struct device *dev,
                  union error_status_t *es)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_error_status",cfg->connector_number);
    if (es == NULL) {
        return -EINVAL;
    }

    /* Port is disabled. Return the last read error_status.
     */
    if (get_state(data) == ST_DISABLE) {
        es->raw_value = data->error_status.raw_value;
        return 0;
    }

    if ((get_state(data) != ST_IDLE) && (get_state(data) != ST_INIT)) {
        return -EBUSY;
    }

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_GET_ERROR_STATUS;
    ucsi_ctrl.data_length=0;
    ucsi_ctrl.command_specific[0] = (ANX_Test_Port_Number_Wraper+1)&0x7f;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,(uint8_t*)es->raw_value,sizeof(union error_status_t),NULL);
}

static int anx74xx_set_rdo(const struct device *dev, uint32_t rdo)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_rdo %x",cfg->connector_number,rdo);
    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }
    data->rdo_as_sink = rdo;
    data->direct_return_cci = true;
    data->internal_ucsi_region.cci.raw_value = 0;
    data->internal_ucsi_region.cci.command_completed = 1;
    set_state(data,ST_CCI_STATUS);
    return 0;
}

static int anx74xx_get_rdo(const struct device *dev, uint32_t *rdo)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_rdo %x",cfg->connector_number, *rdo);
    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    if (rdo == NULL) {
        return -EINVAL;
    }
    *rdo = data->rdo_as_sink;
    data->direct_return_cci = true;
    data->internal_ucsi_region.cci.raw_value = 0;
    data->internal_ucsi_region.cci.command_completed = 1;
    set_state(data,ST_CCI_STATUS);
    return 0;
}

static int anx74xx_get_pdos(const struct device *dev, enum pdo_type_t pdo_type,
              enum pdo_offset_t pdo_offset, uint8_t num_pdos,
              enum pdo_source_t source, uint32_t *pdos)
{
    ANX_DEBUG_PRINT_HEAD
    struct pdc_data_t *data = dev->data;
    //union get_pdos_t *get_pdo;
    ANX_DEBUG("AnxPort%d: anx74xx_get_pdos pdo_type %d, pdo_offset %d, num_pdos %d, source %d",cfg->connector_number, pdo_type, pdo_offset, num_pdos, source);
    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    if (pdos == NULL) {
        return -EINVAL;
    }

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_GET_PDOS;
    ucsi_ctrl.data_length=0;
    ucsi_ctrl.command_specific[0] = ((ANX_Test_Port_Number_Wraper+1)&0x7f) | (source<<7);
    ucsi_ctrl.command_specific[1] = pdo_offset;
    ucsi_ctrl.command_specific[2] = ((num_pdos-1)&0x3) | (pdo_type<<2);
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,(uint8_t*)pdos,sizeof(uint32_t)*num_pdos ,NULL);
}

static int anx74xx_get_info(const struct device *dev, struct pdc_info_t *info,
              bool live)
{
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_info",cfg->connector_number);
    struct pdc_data_t *data = dev->data;
    if (info == NULL) {
        return -EINVAL;
    }
    uint8_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_INFO];
    memset(info,0,sizeof(struct pdc_info_t));
    anx74xx_i2c_read(dev,addr, ANX_PDC_anx74xx_INFO_REG_FW_VERSION,3,(uint8_t*)&info->fw_version);
    anx74xx_i2c_read(dev,addr, ANX_PDC_anx74xx_INFO_REG_PD_VERSION,1,(uint8_t*)&info->pd_version);
    anx74xx_i2c_read(dev,addr, ANX_PDC_anx74xx_INFO_REG_PD_REVERSION,1,(uint8_t*)&info->pd_revision);
    anx74xx_i2c_read(dev,addr, ANX_PDC_anx74xx_INFO_REG_VID,4,(uint8_t*)&info->vid);
    info->is_running_flash_code = 1;
    info->running_in_flash_bank = 0;
    strncpy(info->project_name, STRINGIFY(anx74xx_Pdc),sizeof(info->project_name));
    strncpy(info->driver_name, STRINGIFY(anx74xx_Pdc_Drv),sizeof(info->driver_name));
    ANX_DEBUG("AnxPort%d: anx74xx_get_info, %x %x %x",cfg->connector_number, info->fw_version, info->pd_version, info->pd_revision);
    data->direct_return_cci = true;
    data->internal_ucsi_region.cci.raw_value = 0;
    data->internal_ucsi_region.cci.command_completed = 1;
    set_state(data,ST_CCI_STATUS);
    return 0;
}

static int anx74xx_get_hw_config(const struct device *dev,
                struct pdc_hw_config_t *config)
{
	const struct pdc_config_t *cfg =
		(const struct pdc_config_t *)dev->config;
    ANX_DEBUG("AnxPort%d: anx74xx_get_hw_config",cfg->connector_number);
	if (config == NULL) {
		return -EINVAL;
	}
	config->bus_type = PDC_BUS_TYPE_I2C;
	config->i2c = cfg->i2c;
	config->ccd = cfg->ccd;
    return 0;
}

static bool anx74xx_get_hw_frs_support(const struct device *dev)
{
	const struct pdc_config_t *cfg =
		(const struct pdc_config_t *)dev->config;
    //ANX_DEBUG("AnxPort%d: anx74xx_get_hw_frs_support %d",cfg->connector_number, cfg->frs_supported);
	return cfg->frs_supported;
}

static int anx74xx_get_vbus_voltage(const struct device *dev, uint16_t *voltage)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_vbus_voltage",cfg->connector_number);
    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    if (voltage == NULL) {
        return -EINVAL;
    }

    uint8_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_TCPC];
    uint16_t vol;
    int rv = anx74xx_i2c_read(dev,addr, ANX_PDC_anx74xx_TCPC_REG_VBUS_VOLTAGE,2,(uint8_t*)&vol);
    if(rv != 0){
        return rv;
    }
    vol = (vol & 0x3ff) * 25;
    ANX_DEBUG("AnxPort%d: anx74xx_get_vbus_voltage, %d",cfg->connector_number, vol);
    data->direct_return_cci = true;
    data->internal_ucsi_region.cci.raw_value = 0;
    data->internal_ucsi_region.cci.command_completed = 1;
    set_state(data,ST_CCI_STATUS);
    return 0;
}

static int anx74xx_set_ccom(const struct device *dev, enum ccom_t ccom)
{
    struct pdc_data_t *data = dev->data;
    //uint16_t conn_opmode = 0;
    /*
     * From bit 32, the first 7 bits are connector. The next 4 bits are for
     * the CC operation mode.
     */
    //const uint8_t opmode_offset = 7;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_ccom %d",cfg->connector_number, ccom);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }
    int raw_value = (1<<ccom);
    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_SET_CCOM;
    ucsi_ctrl.data_length = 0;
    ucsi_ctrl.command_specific[0] = ((raw_value&0x01)<<7) | ((ANX_Test_Port_Number_Wraper+1)&0x7f);
    ucsi_ctrl.command_specific[1] = (raw_value&0x0f)>>1;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,NULL,0,NULL);
}

static int anx74xx_set_drp_mode(const struct device *dev, enum drp_mode_t dm)
{
    struct pdc_data_t *data = dev->data;
    //uint8_t opmode = 0;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_drp_mode %d",cfg->connector_number, dm);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }
    uint8_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_EMTB];
    uint8_t val;
    int rv = anx74xx_i2c_read(dev,addr, ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL,1,(uint8_t*)&val);
    if(rv != 0){
        return rv;
    }
    val &= ~(ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_TRY_SNK_EN|ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_TRY_SRC_EN);
    switch (dm)
    {
    case 1:
        val |= ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_TRY_SRC_EN;
        break;
    case 2:
        val |= ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_TRY_SNK_EN;
        break;
    default:
        break;
    }
    rv = anx74xx_i2c_write(dev,addr, ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL,1,(uint8_t*)&val);
    if(rv != 0){
        return rv;
    }
    data->direct_return_cci = true;
    data->internal_ucsi_region.cci.raw_value = 0;
    data->internal_ucsi_region.cci.command_completed = 1;
    set_state(data,ST_CCI_STATUS);
    return 0;
}

static int anx74xx_get_drp_mode(const struct device *dev, enum drp_mode_t *dm)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_drp_mode",cfg->connector_number);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }
    uint8_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_EMTB];
    uint8_t val;
    int rv = anx74xx_i2c_read(dev,addr, ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL,1,(uint8_t*)&val);
    if(rv != 0){
        return rv;
    }
    if(val & ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_TRY_SRC_EN){
        *dm = 1;
    }else if(val & ANX_PDC_anx74xx_EMTB_REG_POLICY_CTRL_TRY_SNK_EN){
        *dm = 2;
    }else{
        *dm = 0;
    }
    ANX_DEBUG("AnxPort%d: anx74xx_get_drp_mode %x",cfg->connector_number, *dm);
    data->direct_return_cci = true;
    data->internal_ucsi_region.cci.raw_value = 0;
    data->internal_ucsi_region.cci.command_completed = 1;
    set_state(data,ST_CCI_STATUS);
    return 0;
}

static int anx74xx_set_uor(const struct device *dev, union uor_t uor)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_uor %x",cfg->connector_number, uor.raw_value);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_SET_UOR;
    ucsi_ctrl.data_length = 0;
    ucsi_ctrl.command_specific[0] = (uor.raw_value&0x80) | ((ANX_Test_Port_Number_Wraper+1)&0x7f);
    ucsi_ctrl.command_specific[1] = (uor.raw_value>>8)&0xff;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,NULL,0,NULL);
}

static int anx74xx_set_pdr(const struct device *dev, union pdr_t pdr)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_pdr %x",cfg->connector_number, pdr.raw_value);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_SET_PDR;
    ucsi_ctrl.data_length = 0;
    ucsi_ctrl.command_specific[0] = (pdr.raw_value&0x80) | ((ANX_Test_Port_Number_Wraper+1)&0x7f);
    ucsi_ctrl.command_specific[1] = (pdr.raw_value>>8)&0xff;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,NULL,0,NULL);
}

static int anx74xx_get_current_pdo(const struct device *dev, uint32_t *pdo)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_current_pdo",cfg->connector_number);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    if (pdo == NULL) {
        return -EINVAL;
    }

    return 0;
}

static int anx74xx_set_frs(const struct device *dev, bool enable)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_frs %d",cfg->connector_number, enable);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }
    data->direct_return_cci = true;
    data->internal_ucsi_region.cci.raw_value = 0;
    data->internal_ucsi_region.cci.command_completed = 1;
    set_state(data,ST_CCI_STATUS);
    return 0;
}

static int anx74xx_set_ap_power_state(const struct device *dev,
				    enum power_state state)
{
	struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_ap_power_state %d",cfg->connector_number, state);

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	return -ENOSYS;
}

static int anx74xx_get_identity_discovery(const struct device *dev,
                    bool *disc_state)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_identity_discovery",cfg->connector_number);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    if (disc_state == NULL) {
        return -EINVAL;
    }

    return 0;
}

static int anx74xx_is_vconn_sourcing(const struct device *dev,
                   bool *vconn_sourcing)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_is_vconn_sourcing",cfg->connector_number);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    if (vconn_sourcing == NULL) {
        return -EINVAL;
    }

    return 0;
}

static int anx74xx_get_pch_data_status(const struct device *dev, uint8_t port_num,
				     uint8_t *status_reg)
{
	struct pdc_data_t *data = dev->data;
	ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_pch_data_status", cfg->connector_number);
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (status_reg == NULL) {
		return -EINVAL;
	}

    uint8_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_PCH];
    int rv = anx74xx_i2c_read(dev, addr, ANX_PDC_anx74xx_PCH_REG_DATA_STATUS,5,(uint8_t*)status_reg);
    ANX_DEBUG("AnxPort%d: anx74xx_get_pch_data_status %x %x %x %x %x",cfg->connector_number, status_reg[0], status_reg[1], status_reg[2], status_reg[3], status_reg[4]);
    if(rv != 0){
        return rv;
    }
    data->direct_return_cci = true;
    data->internal_ucsi_region.cci.raw_value = 0;
    data->internal_ucsi_region.cci.command_completed = 1;
    set_state(data,ST_CCI_STATUS);
	return 0;
}

static void anx74xx_start_thread(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	k_thread_start(data->thread);
}

static bool anx74xx_is_init_done(const struct device *dev)
{
    struct pdc_data_t *data = dev->data;
    //const struct pdc_config_t *cfg = dev->config;
    //ANX_DEBUG("AnxPort%d: anx74xx_is_init_done",cfg->connector_number);

    return data->init_done;
}

static int anx74xx_get_vdo(const struct device *dev, union get_vdo_t vdo_req,
             uint8_t *vdo_req_list, uint32_t *vdo)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_vdo",cfg->connector_number);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    if (vdo == NULL) {
        return -EINVAL;
    }

    data->direct_return_cci = true;
    data->internal_ucsi_region.cci.raw_value = 0;
    data->internal_ucsi_region.cci.command_completed = 1;
    set_state(data,ST_CCI_STATUS);

    return 0;
}

/** Allow 3 seconds for the driver to suspend itself. */
#define SUSPEND_TIMEOUT_USEC (3 * USEC_PER_SEC)

static int anx74xx_set_comms_state(const struct device *dev, bool comms_active)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_comms_state %d",cfg->connector_number,comms_active);

    if (comms_active) {
        /* Re-enable communications. Clearing the suspend flag will
         * trigger a reset. Note: if the driver is in the disabled
         * state due to a previous comms failure, it will remain
         * disabled. (Thus, suspending/resuming comms on a disabled
         * PDC driver is a no-op)
         */
        enable_comms();

    } else {
        /* Request communication to be stopped. This allows in-progress
         * operations to complete first.
         */
        suspend_comms();

        if (get_state(data) == ST_DISABLE) {
            /* The driver is already permanently shut down. */
            return 0;
        }

        /* Wait for driver to enter the suspended state */
        if (!WAIT_FOR((get_state(data) == ST_SUSPENDED),
                  SUSPEND_TIMEOUT_USEC,
                  k_sleep(K_MSEC(T_CCI_STATUS)))) {
            return -ETIMEDOUT;
        }
    }

    return 0;
}

static int anx74xx_set_pdo(const struct device *dev, enum pdo_type_t type,
             uint32_t *pdo, int count)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_pdo type %d, count %d",cfg->connector_number, type ,count);
    for (int i = 0; i < count; i++)
    {
        ANX_DEBUG("AnxPort%d: anx74xx_set_pdo pdo %d, data %x",cfg->connector_number, i ,pdo[i]);
    }
    
    if (pdo == NULL) {
        return -EINVAL;
    }

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_SET_PDOS;
    ucsi_ctrl.data_length = count*sizeof(uint32_t);

    ucsi_ctrl.command_specific[0] = ((ANX_Test_Port_Number_Wraper+1)&0x7f);
    ucsi_ctrl.command_specific[1] = ((type&0x01)<<2)|(count<<3);
    ucsi_ctrl.command_specific[2] = 0x40;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, (uint8_t*)pdo,NULL,0,NULL);
}

#define SMBUS_MAX_BLOCK_SIZE 32

static int anx74xx_execute_ucsi_cmd(const struct device *dev,
                  uint8_t ucsi_command, uint8_t data_size,
                  uint8_t *command_specific,
                  uint8_t *lpm_data_out,
                  struct pdc_callback *callback)
{
    struct pdc_data_t *data = dev->data;
    //uint8_t cmd_buffer[SMBUS_MAX_BLOCK_SIZE];

    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_execute_ucsi_cmd %d, size %d",cfg->connector_number, ucsi_command, data_size);

    if (get_state(data) != ST_IDLE)
        return -EBUSY;

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = ucsi_command;
    //ucsi_ctrl.data_length = data_size; the size is not ucsi message out len
    ucsi_ctrl.data_length = 0;

    for (uint8_t i = 0; i < data_size; i++)
    {
        ucsi_ctrl.command_specific[i] = command_specific[i];
    }
    //the lpm_data_out is not ucsi message out, is message in buf
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL, lpm_data_out,64,callback);
}

static int anx74xx_manage_callback(const struct device *dev,
                 struct pdc_callback *callback, bool set)
{
    struct pdc_data_t *const data = dev->data;
    //const struct pdc_config_t *cfg = dev->config;
    //ANX_DEBUG("AnxPort%d: anx74xx_manage_callback",cfg->connector_number);

    return pdc_manage_callbacks(&data->ci_cb_list, callback, set);
}

static int anx74xx_ack_cc_ci(const struct device *dev,
               union conn_status_change_bits_t ci, bool cc,
               uint16_t vendor_defined)
{
    //struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_ack_cc_ci",cfg->connector_number);
    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_ACK_CC_CI;
    ucsi_ctrl.data_length = 0;
    ucsi_ctrl.command_specific[0] = cc<<1|(ci.raw_value!=0);
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,NULL,0,NULL);
}

static int anx74xx_get_lpm_ppm_info(const struct device *dev,
                  struct lpm_ppm_info_t *info)
{
    struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_lpm_ppm_info",cfg->connector_number);

    if (get_state(data) != ST_IDLE) {
        return -EBUSY;
    }

    if (info == NULL) {
        return -EINVAL;
    }
    memset(info, 0, sizeof(struct lpm_ppm_info_t));
    uint8_t addr = data->i2c_address_map[ANX_PDC_anx74xx_ADDRESS_IND_INFO];
    info->vid = 0x1f29;
    anx74xx_i2c_read(dev,addr, ANX_PDC_anx74xx_INFO_REG_PID,2,(uint8_t*)&info->pid);
    anx74xx_i2c_read(dev,addr, ANX_PDC_anx74xx_INFO_REG_FW_VERSION,3,(uint8_t*)&info->fw_ver);
    anx74xx_i2c_read(dev,addr, ANX_PDC_anx74xx_INFO_REG_CSTM_VERSION,2,(uint8_t*)&info->fw_ver_sub);
    ANX_DEBUG("AnxPort%d: anx74xx_get_lpm_ppm_info %x %x %x",cfg->connector_number, info->pid, info->fw_ver, info->fw_ver_sub);
    data->direct_return_cci = true;
    data->internal_ucsi_region.cci.raw_value = 0;
    data->internal_ucsi_region.cci.command_completed = 1;
    set_state(data,ST_CCI_STATUS);
    return 0;
}

static int anx74xx_get_attention_vdo(const struct device *dev,
				   union get_attention_vdo_t *vdo)
{
	struct pdc_data_t *data = dev->data;
	ANX_DEBUG_PRINT_HEAD
	ANX_DEBUG("AnxPort%d: anx74xx_get_attention_vdo",cfg->connector_number);

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (vdo == NULL) {
		return -EINVAL;
	}

    struct ucsi_control_t ucsi_ctrl;
    memset(&ucsi_ctrl,0,sizeof(struct ucsi_control_t));
    ucsi_ctrl.command = UCSI_GET_ATTENTION_VDO;
    ucsi_ctrl.data_length = 0;
    ucsi_ctrl.command_specific[0] = (ANX_Test_Port_Number_Wraper+1)&0x7f;
    return anx74xx_fill_ucsi_bp_st_write_cmd_with_callback(dev, ucsi_ctrl, NULL,(uint8_t*)&vdo->raw_value,sizeof(union get_attention_vdo_t),NULL);
}

#ifdef CONFIG_USBC_PDC_DRIVEN_CCD
static int anx74xx_get_sbu_mux_mode(const struct device *dev,
				  enum pdc_sbu_mux_mode *mode)
{
	struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_sbu_mux_mode",cfg->connector_number);
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (mode == NULL) {
		return -EINVAL;
	}

	return -ENOSYS;
}

static int anx74xx_set_sbu_mux_mode(const struct device *dev,
				  enum pdc_sbu_mux_mode mode)
{
	struct pdc_data_t *data = dev->data;
	uint8_t setting;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_sbu_mux_mode %d",cfg->connector_number, mode);
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	switch (mode) {
	case PDC_SBU_MUX_MODE_NORMAL:
		setting = 0;
		break;
	case PDC_SBU_MUX_MODE_FORCE_DBG:
		setting = 1;
		break;
	default:
		return -ERANGE;
	}

	return -ENOSYS;
}
#endif /* defined(CONFIG_USBC_PDC_DRIVEN_CCD) */

static int anx74xx_set_battery_capability(const struct device *dev,
					union battery_capability_t *bcap)
{
	struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_battery_capability %x %x %x %x %x",cfg->connector_number, bcap->vid, bcap->pid,bcap->design_capacity, bcap->last_full_charge_capacity, bcap->type);
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	return -ENOSYS;
}

static int anx74xx_set_battery_status(const struct device *dev,
				    union battery_status_t *bstat)
{
	struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_set_battery_status %x %x %x %x",cfg->connector_number, bstat->battery_present, bstat->battery_state, bstat->flags, bstat->present_capacity);
	
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	return -ENOSYS;
}

static int anx74xx_set_bbr_cts(const struct device *dev, bool enable)
{
	ANX_DEBUG_PRINT_HEAD

	struct pdc_data_t *data = dev->data;
    ANX_DEBUG("AnxPort%d: anx74xx_set_bbr_cts %x",cfg->connector_number, enable);
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	return -ENOSYS;
}

static int
anx74xx_get_vendor_status(const struct device *dev,
			union vendor_status_change_bits_t *vendor_status_change)
{
	struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_vendor_status",cfg->connector_number);
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (vendor_status_change == NULL) {
		return -EINVAL;
	}

	return -ENOSYS;
}

static int anx74xx_get_alert(const struct device *dev, uint32_t *ado)
{
	struct pdc_data_t *data = dev->data;
    ANX_DEBUG_PRINT_HEAD
    ANX_DEBUG("AnxPort%d: anx74xx_get_vendor_status",cfg->connector_number);
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (ado == NULL) {
		return -EINVAL;
	}

	return -ENOSYS;
}

static DEVICE_API(pdc, pdc_driver_api) = {
    .start_thread = anx74xx_start_thread,
    .is_init_done = anx74xx_is_init_done,
    .get_ucsi_version = anx74xx_get_ucsi_version,
    .reset = anx74xx_pdc_reset,
    .connector_reset = anx74xx_connector_reset,
    .get_capability = anx74xx_get_capability,
    .get_connector_capability = anx74xx_get_connector_capability,
    .set_ccom = anx74xx_set_ccom,
    .set_drp_mode = anx74xx_set_drp_mode,
    .get_drp_mode = anx74xx_get_drp_mode,
    .set_uor = anx74xx_set_uor,
    .set_pdr = anx74xx_set_pdr,
    .set_sink_path = anx74xx_set_sink_path,
    .get_connector_status = anx74xx_get_connector_status,
    .get_pdos = anx74xx_get_pdos,
    .get_rdo = anx74xx_get_rdo,
    .set_rdo = anx74xx_set_rdo,
    .get_error_status = anx74xx_get_error_status,
    .get_vbus_voltage = anx74xx_get_vbus_voltage,
    .get_current_pdo = anx74xx_get_current_pdo,
    .set_handler_cb = anx74xx_set_handler_cb,
    .read_power_level = anx74xx_read_power_level,
    .get_info = anx74xx_get_info,
    .get_hw_config = anx74xx_get_hw_config,
	.get_hw_frs_support = anx74xx_get_hw_frs_support,
    .set_power_level = anx74xx_set_power_level,
    .reconnect = anx74xx_reconnect,
    .update_retimer = anx74xx_set_retimer_update_mode,
    .get_cable_property = anx74xx_get_cable_property,
    .get_vdo = anx74xx_get_vdo,
    .get_identity_discovery = anx74xx_get_identity_discovery,
    .set_comms_state = anx74xx_set_comms_state,
    .is_vconn_sourcing = anx74xx_is_vconn_sourcing,
    .set_pdos = anx74xx_set_pdo,
    .get_pch_data_status = anx74xx_get_pch_data_status,
    .execute_ucsi_cmd = anx74xx_execute_ucsi_cmd,
    .manage_callback = anx74xx_manage_callback,
    .ack_cc_ci = anx74xx_ack_cc_ci,
    .get_lpm_ppm_info = anx74xx_get_lpm_ppm_info,
    .set_frs = anx74xx_set_frs,
    .get_attention_vdo = anx74xx_get_attention_vdo,
	.set_battery_capability = anx74xx_set_battery_capability,
	.set_battery_status = anx74xx_set_battery_status,
#ifdef CONFIG_USBC_PDC_DRIVEN_CCD
	.get_sbu_mux_mode = anx74xx_get_sbu_mux_mode,
	.set_sbu_mux_mode = anx74xx_set_sbu_mux_mode,
#endif /* define(CONFIG_USBC_PDC_DRIVEN_CCD) */
	.set_bbr_cts = anx74xx_set_bbr_cts,
	.set_ap_power_state = anx74xx_set_ap_power_state,
	.get_vendor_status = anx74xx_get_vendor_status,
	.get_alert = anx74xx_get_alert,
};

static int pdc_init(const struct device *dev)
{
    const struct pdc_config_t *cfg = dev->config;
    struct pdc_data_t *data = dev->data;
    int rv;
    LOG_INF("Anx PDC Build " __DATE__ " " __TIME__ );
    rv = i2c_is_ready_dt(&cfg->i2c);
    if (rv < 0) {
        LOG_ERR("device %s not ready", cfg->i2c.bus->name);
        return -ENODEV;
    }

    rv = gpio_is_ready_dt(&cfg->irq_gpios);
    if (rv < 0) {
        LOG_ERR("device %s not ready", cfg->irq_gpios.port->name);
        return -ENODEV;
    }

    for (uint8_t i = 0; i < ANX_PDC_anx74xx_ADDRESS_MAP_COUNT; i++) {
        if(cfg->i2c.addr == ANX_PDC_anx74xx_ADDRESS_MAP[i][0]) {
            memcpy(data->i2c_address_map, &ANX_PDC_anx74xx_ADDRESS_MAP[i][0], ANX_PDC_anx74xx_ADDRESS_COUNT);
            data->internal_ucsi_region_p1 = (i>=(ANX_PDC_anx74xx_ADDRESS_MAP_COUNT/2));
            break;
        }
    }

    k_event_init(&data->driver_event);

    irq_event[cfg->connector_number] = &data->driver_event;

    rv = gpio_pin_configure_dt(&cfg->irq_gpios, GPIO_INPUT);
    if (rv < 0) {
        LOG_ERR("Unable to configure GPIO");
        return rv;
    }

    gpio_init_callback(&data->gpio_cb, cfg->callback_handler,
        BIT(cfg->irq_gpios.pin));

    rv = gpio_add_callback(cfg->irq_gpios.port, &data->gpio_cb);
    if (rv < 0) {
        LOG_ERR("Unable to add callback");
        return rv;
    }

    rv = gpio_pin_interrupt_configure_dt(&cfg->irq_gpios,
                            GPIO_INT_EDGE_FALLING);
    if (rv < 0) {
        LOG_ERR("Unable to configure interrupt");
        return rv;
    }
    LOG_INF("PDC%d: irq enabled", cfg->connector_number);
    /* Trigger IRQ on startup to read any pending interrupts */
    k_event_post(irq_event[cfg->connector_number], ANX74XX_IRQ_EVENT);

    k_mutex_init(&data->mtx);

    data->dev = dev;
    data->error_recovery_counter = 0;
    data->init_retry_counter = 0;
    data->info.fw_version = PDC_FWVER_INVALID;

    pdc_data[cfg->connector_number] = data;
    /* Set initial state */
    data->init_local_state = INIT_PDC_WAKEUP;
    smf_set_initial(SMF_CTX(data), &states[ST_INIT]);

    /* Create the thread for this port */
    cfg->create_thread(dev);

    //LOG_INF("PDC%d: AnalogixSemi anx74xx Chip Addr Map %x, Internal Port%x", cfg->connector_number, data->i2c_address_map[0], data->internal_ucsi_region_p1);

    return 0;
}

static void anx74xx_thread(void *dev, void *unused1, void *unused2)
{
    const struct pdc_config_t *cfg = ((const struct device *)dev)->config;
    struct pdc_data_t *data = ((const struct device *)dev)->data;
    uint32_t events;
    bool irq_pending = false;

    while (1) {
        smf_run_state(SMF_CTX(data));

        events = k_event_wait(&data->driver_event,
            ANX74XX_IRQ_EVENT |
                          anx74xx_NEXT_STATE_READY,
                      false, K_MSEC(2000));

        if (events & ANX74XX_IRQ_EVENT) {
            irq_pending = true;
        }

        //ANX_DEBUG("AnxPort%d: get irq pin %d, evt %x, state %d, init_done %d",cfg->connector_number, gpio_pin_get_dt(&cfg->irq_gpios), irq_pending, get_state(data), data->init_done);

        k_event_clear(&data->driver_event, events);

        /* We not only handle irq on idle. */
        if (irq_pending) {
            if (check_comms_suspended()) {
                LOG_INF("PDC%d: Ignoring interrupt",
                    cfg->connector_number);
                continue;
            }
            if(get_state(data)==ST_IDLE){
                if(handle_irqs(data)==0)
                {
                    irq_pending = false;
                }
            }
        }
    }
}

#define PDC_DATA_STRUCT_NAME(inst) pdc_data_##inst

BUILD_ASSERT(
	EC_TASK_PRIORITY(EC_TASK_USBC_PDC_PRIO) ==
		CONFIG_USBC_PDC_ANX74XX_THREAD_PRIORITY,
	"EC_TASK_USBC_PDC_PRIO does not match USBC_PDC_RTS54XX_THREAD_PRIORITY.");

#define ANX74XX_PDC_DEFINE(inst)                                            \
    K_THREAD_STACK_DEFINE(anx74xx_thread_stack_area_##inst,                 \
                  CONFIG_USBC_PDC_ANX74XX_STACK_SIZE);                      \
                                                                            \
    static void create_thread_##inst(const struct device *dev)              \
    {                                                                       \
        struct pdc_data_t *data = dev->data;                                \
                                                                            \
        data->thread = k_thread_create(                                     \
            &data->thread_data, anx74xx_thread_stack_area_##inst,         \
            K_THREAD_STACK_SIZEOF(                         \
	    anx74xx_thread_stack_area_##inst),      \
            anx74xx_thread, (void *)dev, 0, 0,                    \
            CONFIG_USBC_PDC_ANX74XX_THREAD_PRIORITY, K_ESSENTIAL, \
            K_FOREVER);                                           \
        k_thread_name_set(data->thread, "ANX74XX" STRINGIFY(inst));   \
    }                                                                     \
                                                                              \
    static struct pdc_data_t PDC_DATA_STRUCT_NAME(inst);                      \
                                                                              \
	static void pdc_interrupt_callback##inst(const struct device *dev,    \
						 struct gpio_callback *cb,    \
						 uint32_t pins)               \
	{                                                                     \
		k_event_post(&pdc_data_##inst.driver_event,                   \
            ANX74XX_IRQ_EVENT);                              \
	}                                                                     \
                                                                              \
    static const struct pdc_config_t pdc_config##inst = {                 \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                            \
        .irq_gpios = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),          \
        .connector_number =                                           \
                USBC_PORT_FROM_PDC_DRIVER_NODE(DT_DRV_INST(inst)),    \
        .bits.command_completed = 1,                                  \
        .bits.external_supply_change = 1,                             \
        .bits.power_operation_mode_change = 1,                        \
        .bits.attention = 1,                                          \
        .bits.fw_update_request = 0,                                  \
        .bits.provider_capability_change_supported = 1,               \
        .bits.negotiated_power_level_change = 1,                      \
        .bits.pd_reset_complete = 1,                                  \
        .bits.support_cam_change = 1,                                 \
        .bits.battery_charging_status_change = 1,                     \
        .bits.security_request_from_port_partner = 0,                 \
        .bits.connector_partner_change = 1,                           \
        .bits.power_direction_change = 1,                             \
        .bits.set_retimer_mode = 0,                                   \
        .bits.connect_change = 1,                                     \
        .bits.error = 1,                                              \
        .bits.sink_path_status_change = 1,                            \
        .create_thread = create_thread_##inst,                        \
        .no_fw_update = DT_INST_PROP(inst, no_fw_update),             \
        .ccd = DT_INST_PROP(inst, ccd),                               \
        .frs_supported = DT_INST_PROP(inst, frs_supported),           \
        .callback_handler = pdc_interrupt_callback##inst,             \
    };                                                                    \
                                                                              \
	DEVICE_DT_INST_DEFINE(inst, pdc_init, NULL,                           \
			      &PDC_DATA_STRUCT_NAME(inst), &pdc_config##inst, \
			      POST_KERNEL, CONFIG_PDC_DRIVER_INIT_PRIORITY,   \
			      &pdc_driver_api);                               \
                                                                              \
	static struct pdc_data_t PDC_DATA_STRUCT_NAME(inst) = {               \
		.dev = DEVICE_DT_INST_GET(inst),                              \
	};

DT_INST_FOREACH_STATUS_OKAY(ANX74XX_PDC_DEFINE)

