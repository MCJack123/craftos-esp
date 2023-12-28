#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_private/spi_common_internal.h>
#include <freertos/queue.h>
#include <hal/spi_hal.h>
#include <hal/spi_ll.h>
#include <hal/gdma_ll.h>
#include <soc/lldesc.h>
#include <soc/gdma_struct.h>
#include <soc/io_mux_reg.h>
#include "vga.h"

static const char* const TAG = "vga";
// framebuffer transfer is split into multiple chunks
DMA_ATTR static uint8_t framebuffer_vsync[FB_WIDTH*3];   // 3/445 lines
DMA_ATTR static uint8_t framebuffer_chunk1[FB_WIDTH*68]; // 71/445 lines
DMA_ATTR static uint8_t framebuffer_chunk2[FB_WIDTH*68]; // 139/445 lines
DMA_ATTR static uint8_t framebuffer_chunk3[FB_WIDTH*68]; // 207/445 lines
DMA_ATTR static uint8_t framebuffer_chunk4[FB_WIDTH*68]; // 275/445 lines
DMA_ATTR static uint8_t framebuffer_chunk5[FB_WIDTH*68]; // 343/445 lines
DMA_ATTR static uint8_t framebuffer_chunk6[FB_WIDTH*68]; // 411/445 lines
DMA_ATTR static uint8_t framebuffer_chunk7[FB_WIDTH*34]; // 445/445 lines
DMA_ATTR static lldesc_t llchunks[12][12];
DMA_ATTR static uint32_t dma_conf[4];
DMA_ATTR static uint32_t dma_conf_last[4];
DMA_ATTR static uint32_t dma_conf_vsync[4];

/* from spi_master.c; Apache 2.0 */

#define spi_dma_ll_rx_reset(dev, chan)                             gdma_ll_rx_reset_channel(&GDMA, chan)
#define spi_dma_ll_tx_reset(dev, chan)                             gdma_ll_tx_reset_channel(&GDMA, chan);
#define spi_dma_ll_rx_start(dev, chan, addr) do {\
            gdma_ll_rx_set_desc_addr(&GDMA, chan, (uint32_t)addr);\
            gdma_ll_rx_start(&GDMA, chan);\
        } while (0)
#define spi_dma_ll_tx_start(dev, chan, addr) do {\
            gdma_ll_tx_set_desc_addr(&GDMA, chan, (uint32_t)addr);\
            gdma_ll_tx_start(&GDMA, chan);\
        } while (0)

static const char* const SPI_TAG = "spi_master";
#define SPI_CHECK(a, str, ret_val)  ESP_RETURN_ON_FALSE_ISR(a, ret_val, SPI_TAG, str)

struct spi_bus_lock_t;
struct spi_bus_lock_dev_t;
/// Handle to the lock of an SPI bus
typedef struct spi_bus_lock_t* spi_bus_lock_handle_t;
/// Handle to lock of one of the device on an SPI bus
typedef struct spi_bus_lock_dev_t* spi_bus_lock_dev_handle_t;

/// Background operation control function
typedef void (*bg_ctrl_func_t)(void*);

typedef struct lldesc_s lldesc_t;

typedef struct spi_device_t spi_device_t;

/// struct to hold private transaction data (like tx and rx buffer for DMA).
typedef struct {
    spi_transaction_t   *trans;
    const uint32_t *buffer_to_send;   //equals to tx_data, if SPI_TRANS_USE_RXDATA is applied; otherwise if original buffer wasn't in DMA-capable memory, this gets the address of a temporary buffer that is;
                                //otherwise sets to the original buffer or NULL if no buffer is assigned.
    uint32_t *buffer_to_rcv;    // similar to buffer_to_send
} spi_trans_priv_t;

typedef struct {
    int id;
    spi_device_t* device[6];
    intr_handle_t intr;
    spi_hal_context_t hal;
    spi_trans_priv_t cur_trans_buf;
    int cur_cs;     //current device doing transaction
    const spi_bus_attr_t* bus_attr;

    /**
     * the bus is permanently controlled by a device until `spi_bus_release_bus`` is called. Otherwise
     * the acquiring of SPI bus will be freed when `spi_device_polling_end` is called.
     */
    spi_device_t* device_acquiring_lock;

//debug information
    bool polling;   //in process of a polling, avoid of queue new transactions into ISR
} spi_host_t;

struct spi_device_t {
    int id;
    int real_clk_freq_hz;
    QueueHandle_t trans_queue;
    QueueHandle_t ret_queue;
    spi_device_interface_config_t cfg;
    spi_hal_dev_config_t hal_dev;
    spi_host_t *host;
    spi_bus_lock_dev_handle_t dev_lock;
};

static IRAM_ATTR esp_err_t check_trans_valid(spi_device_handle_t handle, spi_transaction_t *trans_desc)
{
    SPI_CHECK(handle!=NULL, "invalid dev handle", ESP_ERR_INVALID_ARG);
    spi_host_t *host = handle->host;
    const spi_bus_attr_t* bus_attr = host->bus_attr;
    bool tx_enabled = (trans_desc->flags & SPI_TRANS_USE_TXDATA) || (trans_desc->tx_buffer);
    bool rx_enabled = (trans_desc->flags & SPI_TRANS_USE_RXDATA) || (trans_desc->rx_buffer);
    spi_transaction_ext_t *t_ext = (spi_transaction_ext_t *)trans_desc;
    bool dummy_enabled = (((trans_desc->flags & SPI_TRANS_VARIABLE_DUMMY)? t_ext->dummy_bits: handle->cfg.dummy_bits) != 0);
    bool extra_dummy_enabled = handle->hal_dev.timing_conf.timing_dummy;
    bool is_half_duplex = ((handle->cfg.flags & SPI_DEVICE_HALFDUPLEX) != 0);

    //check transmission length
    SPI_CHECK((trans_desc->flags & SPI_TRANS_USE_RXDATA)==0 || trans_desc->rxlength <= 32, "SPI_TRANS_USE_RXDATA only available for rxdata transfer <= 32 bits", ESP_ERR_INVALID_ARG);
    SPI_CHECK((trans_desc->flags & SPI_TRANS_USE_TXDATA)==0 || trans_desc->length <= 32, "SPI_TRANS_USE_TXDATA only available for txdata transfer <= 32 bits", ESP_ERR_INVALID_ARG);
    SPI_CHECK(trans_desc->length <= bus_attr->max_transfer_sz*8, "txdata transfer > host maximum", ESP_ERR_INVALID_ARG);
    SPI_CHECK(trans_desc->rxlength <= bus_attr->max_transfer_sz*8, "rxdata transfer > host maximum", ESP_ERR_INVALID_ARG);
    SPI_CHECK(is_half_duplex || trans_desc->rxlength <= trans_desc->length, "rx length > tx length in full duplex mode", ESP_ERR_INVALID_ARG);
    //check working mode
#if SOC_SPI_SUPPORT_OCT
    SPI_CHECK(!(host->id == SPI3_HOST && trans_desc->flags & SPI_TRANS_MODE_OCT), "SPI3 does not support octal mode", ESP_ERR_INVALID_ARG);
    SPI_CHECK(!((trans_desc->flags & SPI_TRANS_MODE_OCT) && (handle->cfg.flags & SPI_DEVICE_3WIRE)), "Incompatible when setting to both Octal mode and 3-wire-mode", ESP_ERR_INVALID_ARG);
    SPI_CHECK(!((trans_desc->flags & SPI_TRANS_MODE_OCT) && !is_half_duplex), "Incompatible when setting to both Octal mode and half duplex mode", ESP_ERR_INVALID_ARG);
#endif
    SPI_CHECK(!((trans_desc->flags & (SPI_TRANS_MODE_DIO|SPI_TRANS_MODE_QIO)) && (handle->cfg.flags & SPI_DEVICE_3WIRE)), "Incompatible when setting to both multi-line mode and 3-wire-mode", ESP_ERR_INVALID_ARG);
    SPI_CHECK(!((trans_desc->flags & (SPI_TRANS_MODE_DIO|SPI_TRANS_MODE_QIO)) && !is_half_duplex), "Incompatible when setting to both multi-line mode and half duplex mode", ESP_ERR_INVALID_ARG);
#ifdef CONFIG_IDF_TARGET_ESP32
    SPI_CHECK(!is_half_duplex || !bus_attr->dma_enabled || !rx_enabled || !tx_enabled, "SPI half duplex mode does not support using DMA with both MOSI and MISO phases.", ESP_ERR_INVALID_ARG );
#endif
#if !SOC_SPI_HD_BOTH_INOUT_SUPPORTED
    //On these chips, HW doesn't support using both TX and RX phases when in halfduplex mode
    SPI_CHECK(!is_half_duplex || !tx_enabled || !rx_enabled, "SPI half duplex mode is not supported when both MOSI and MISO phases are enabled.", ESP_ERR_INVALID_ARG);
    SPI_CHECK(!is_half_duplex || !trans_desc->length || !trans_desc->rxlength, "SPI half duplex mode is not supported when both MOSI and MISO phases are enabled.", ESP_ERR_INVALID_ARG);
#endif
    //MOSI phase is skipped only when both tx_buffer and SPI_TRANS_USE_TXDATA are not set.
    SPI_CHECK(trans_desc->length != 0 || !tx_enabled, "trans tx_buffer should be NULL and SPI_TRANS_USE_TXDATA should be cleared to skip MOSI phase.", ESP_ERR_INVALID_ARG);
    //MISO phase is skipped only when both rx_buffer and SPI_TRANS_USE_RXDATA are not set.
    //If set rxlength=0 in full_duplex mode, it will be automatically set to length
    SPI_CHECK(!is_half_duplex || trans_desc->rxlength != 0 || !rx_enabled, "trans rx_buffer should be NULL and SPI_TRANS_USE_RXDATA should be cleared to skip MISO phase.", ESP_ERR_INVALID_ARG);
    //In Full duplex mode, default rxlength to be the same as length, if not filled in.
    // set rxlength to length is ok, even when rx buffer=NULL
    if (trans_desc->rxlength==0 && !is_half_duplex) {
        trans_desc->rxlength=trans_desc->length;
    }
    //Dummy phase is not available when both data out and in are enabled, regardless of FD or HD mode.
    SPI_CHECK(!tx_enabled || !rx_enabled || !dummy_enabled || !extra_dummy_enabled, "Dummy phase is not available when both data out and in are enabled", ESP_ERR_INVALID_ARG);

    if (bus_attr->dma_enabled) {
        SPI_CHECK(trans_desc->length <= SPI_LL_DMA_MAX_BIT_LEN, "txdata transfer > hardware max supported len", ESP_ERR_INVALID_ARG);
        SPI_CHECK(trans_desc->rxlength <= SPI_LL_DMA_MAX_BIT_LEN, "rxdata transfer > hardware max supported len", ESP_ERR_INVALID_ARG);
    } else {
        SPI_CHECK(trans_desc->length <= SPI_LL_CPU_MAX_BIT_LEN, "txdata transfer > hardware max supported len", ESP_ERR_INVALID_ARG);
        SPI_CHECK(trans_desc->rxlength <= SPI_LL_CPU_MAX_BIT_LEN, "rxdata transfer > hardware max supported len", ESP_ERR_INVALID_ARG);
    }

    return ESP_OK;
}

static IRAM_ATTR void uninstall_priv_desc(spi_trans_priv_t* trans_buf)
{
    spi_transaction_t *trans_desc = trans_buf->trans;
    if ((void *)trans_buf->buffer_to_send != &trans_desc->tx_data[0] &&
        trans_buf->buffer_to_send != trans_desc->tx_buffer) {
        free((void *)trans_buf->buffer_to_send); //force free, ignore const
    }
    // copy data from temporary DMA-capable buffer back to IRAM buffer and free the temporary one.
    if (trans_buf->buffer_to_rcv &&
        (void *)trans_buf->buffer_to_rcv != &trans_desc->rx_data[0] &&
        trans_buf->buffer_to_rcv != trans_desc->rx_buffer) { // NOLINT(clang-analyzer-unix.Malloc)
        if (trans_desc->flags & SPI_TRANS_USE_RXDATA) {
            memcpy((uint8_t *) & trans_desc->rx_data[0], trans_buf->buffer_to_rcv, (trans_desc->rxlength + 7) / 8);
        } else {
            memcpy(trans_desc->rx_buffer, trans_buf->buffer_to_rcv, (trans_desc->rxlength + 7) / 8);
        }
        free(trans_buf->buffer_to_rcv);
    }
}

static IRAM_ATTR esp_err_t setup_priv_desc(spi_transaction_t *trans_desc, spi_trans_priv_t* new_desc, bool isdma)
{
    *new_desc = (spi_trans_priv_t) { .trans = trans_desc, };

    // rx memory assign
    uint32_t* rcv_ptr;
    if ( trans_desc->flags & SPI_TRANS_USE_RXDATA ) {
        rcv_ptr = (uint32_t *)&trans_desc->rx_data[0];
    } else {
        //if not use RXDATA neither rx_buffer, buffer_to_rcv assigned to NULL
        rcv_ptr = trans_desc->rx_buffer;
    }
    if (rcv_ptr && isdma && (!esp_ptr_dma_capable(rcv_ptr) || ((int)rcv_ptr % 4 != 0))) {
        //if rxbuf in the desc not DMA-capable, malloc a new one. The rx buffer need to be length of multiples of 32 bits to avoid heap corruption.
        ESP_LOGD(SPI_TAG, "Allocate RX buffer for DMA" );
        rcv_ptr = heap_caps_malloc(((trans_desc->rxlength + 31) / 32) * 4, MALLOC_CAP_DMA);
        if (rcv_ptr == NULL) goto clean_up;
    }
    new_desc->buffer_to_rcv = rcv_ptr;

    // tx memory assign
    const uint32_t *send_ptr;
    if ( trans_desc->flags & SPI_TRANS_USE_TXDATA ) {
        send_ptr = (uint32_t *)&trans_desc->tx_data[0];
    } else {
        //if not use TXDATA neither tx_buffer, tx data assigned to NULL
        send_ptr = trans_desc->tx_buffer ;
    }
    if (send_ptr && isdma && !esp_ptr_dma_capable( send_ptr )) {
        //if txbuf in the desc not DMA-capable, malloc a new one
        ESP_LOGD(SPI_TAG, "Allocate TX buffer for DMA" );
        uint32_t *temp = heap_caps_malloc((trans_desc->length + 7) / 8, MALLOC_CAP_DMA);
        if (temp == NULL) goto clean_up;

        memcpy( temp, send_ptr, (trans_desc->length + 7) / 8 );
        send_ptr = temp;
    }
    new_desc->buffer_to_send = send_ptr;

    return ESP_OK;

clean_up:
    uninstall_priv_desc(new_desc);
    return ESP_ERR_NO_MEM;
}

static void IRAM_ATTR spi_hal_prepare_data_(spi_hal_context_t *hal, const spi_hal_dev_config_t *dev, const spi_hal_trans_config_t *trans)
{
    spi_dev_t *hw = hal->hw;

    //Fill DMA descriptors
    if (trans->rcv_buffer) {
        if (!hal->dma_enabled) {
            //No need to setup anything; we'll copy the result out of the work registers directly later.
        } else {
            lldesc_setup_link(hal->dmadesc_rx, trans->rcv_buffer, ((trans->rx_bitlen + 7) / 8), true);

            spi_dma_ll_rx_reset(hal->dma_in, hal->rx_dma_chan);
            spi_ll_dma_rx_fifo_reset(hal->hw);
            spi_ll_infifo_full_clr(hal->hw);
            spi_ll_dma_rx_enable(hal->hw, 1);
            spi_dma_ll_rx_start(hal->dma_in, hal->rx_dma_chan, hal->dmadesc_rx);
        }

    }
#if CONFIG_IDF_TARGET_ESP32
    else {
        //DMA temporary workaround: let RX DMA work somehow to avoid the issue in ESP32 v0/v1 silicon
        if (hal->dma_enabled && !dev->half_duplex) {
            spi_ll_dma_rx_enable(hal->hw, 1);
            spi_dma_ll_rx_start(hal->dma_in, hal->rx_dma_chan, 0);
        }
    }
#endif

    if (trans->send_buffer) {
        if (!hal->dma_enabled) {
            //Need to copy data to registers manually
            spi_ll_write_buffer(hw, trans->send_buffer, trans->tx_bitlen);
        } else {
            //lldesc_setup_link(hal->dmadesc_tx, trans->send_buffer, (trans->tx_bitlen + 7) / 8, false);
            hal->dmadesc_tx = llchunks[0];

            spi_dma_ll_tx_reset(hal->dma_out, hal->tx_dma_chan);
            spi_ll_dma_tx_fifo_reset(hal->hw);
            spi_ll_outfifo_empty_clr(hal->hw);
            spi_ll_dma_tx_enable(hal->hw, 1);
            spi_dma_ll_tx_start(hal->dma_out, hal->tx_dma_chan, hal->dmadesc_tx);
        }
    }

    //in ESP32 these registers should be configured after the DMA is set
    if ((!dev->half_duplex && trans->rcv_buffer) || trans->send_buffer) {
        spi_ll_enable_mosi(hw, 1);
        spi_ll_set_mosi_bitlen(hw, trans->tx_bitlen);
    } else {
        spi_ll_enable_mosi(hw, 0);
    }
    spi_ll_enable_miso(hw, (trans->rcv_buffer) ? 1 : 0);
}

// Setup the device-specified configuration registers. Called every time a new
// transaction is to be sent, but only apply new configurations when the device
// changes.
static SPI_MASTER_ISR_ATTR void spi_setup_device(spi_device_t *dev)
{
    spi_bus_lock_dev_handle_t dev_lock = dev->dev_lock;
    spi_hal_context_t *hal = &dev->host->hal;
    spi_hal_dev_config_t *hal_dev = &(dev->hal_dev);

    if (spi_bus_lock_touch(dev_lock)) {
        /* Configuration has not been applied yet. */
        spi_hal_setup_device(hal, hal_dev);
    }
}

// The function is called to send a new transaction, in ISR or in the task.
// Setup the transaction-specified registers and linked-list used by the DMA (or FIFO if DMA is not used)
static void IRAM_ATTR spi_new_trans(spi_device_t *dev, spi_trans_priv_t *trans_buf)
{
    spi_transaction_t *trans = trans_buf->trans;
    spi_host_t *host = dev->host;
    spi_hal_context_t *hal = &(host->hal);
    spi_hal_dev_config_t *hal_dev = &(dev->hal_dev);

    host->cur_cs = dev->id;

    //Reconfigure according to device settings, the function only has effect when the dev_id is changed.
    spi_setup_device(dev);

    //set the transaction specific configuration each time before a transaction setup
    spi_hal_trans_config_t hal_trans = {};
    hal_trans.tx_bitlen = trans->length;
    hal_trans.rx_bitlen = trans->rxlength;
    hal_trans.rcv_buffer = (uint8_t*)host->cur_trans_buf.buffer_to_rcv;
    hal_trans.send_buffer = (uint8_t*)host->cur_trans_buf.buffer_to_send;
    hal_trans.cmd = trans->cmd;
    hal_trans.addr = trans->addr;
    hal_trans.cs_keep_active = (trans->flags & SPI_TRANS_CS_KEEP_ACTIVE) ? 1 : 0;

    //Set up OIO/QIO/DIO if needed
    hal_trans.line_mode.data_lines = (trans->flags & SPI_TRANS_MODE_DIO) ? 2 :
        (trans->flags & SPI_TRANS_MODE_QIO) ? 4 : 1;
#if SOC_SPI_SUPPORT_OCT
    if (trans->flags & SPI_TRANS_MODE_OCT) {
        hal_trans.line_mode.data_lines = 8;
    }
#endif
    hal_trans.line_mode.addr_lines = (trans->flags & SPI_TRANS_MULTILINE_ADDR) ? hal_trans.line_mode.data_lines : 1;
    hal_trans.line_mode.cmd_lines = (trans->flags & SPI_TRANS_MULTILINE_CMD) ? hal_trans.line_mode.data_lines : 1;

    if (trans->flags & SPI_TRANS_VARIABLE_CMD) {
        hal_trans.cmd_bits = ((spi_transaction_ext_t *)trans)->command_bits;
    } else {
        hal_trans.cmd_bits = dev->cfg.command_bits;
    }
    if (trans->flags & SPI_TRANS_VARIABLE_ADDR) {
        hal_trans.addr_bits = ((spi_transaction_ext_t *)trans)->address_bits;
    } else {
        hal_trans.addr_bits = dev->cfg.address_bits;
    }
    if (trans->flags & SPI_TRANS_VARIABLE_DUMMY) {
        hal_trans.dummy_bits = ((spi_transaction_ext_t *)trans)->dummy_bits;
    } else {
        hal_trans.dummy_bits = dev->cfg.dummy_bits;
    }

    spi_hal_setup_trans(hal, hal_dev, &hal_trans);

    dma_conf_vsync[2] = (hal->hw->misc.val | SPI_CS0_DIS) & ~SPI_CS_KEEP_ACTIVE;
    dma_conf[2] = (hal->hw->misc.val & ~SPI_CS0_DIS) | SPI_CS_KEEP_ACTIVE;
    dma_conf_last[2] = hal->hw->misc.val & ~(SPI_CS0_DIS | SPI_CS_KEEP_ACTIVE);
    hal->hw->slave.dma_seg_magic_value = 0xA;

    spi_hal_prepare_data_(hal, hal_dev, &hal_trans);

    //Call pre-transmission callback, if any
    if (dev->cfg.pre_cb) dev->cfg.pre_cb(trans);

    //Kick off transfer
    hal->hw->slave.usr_conf = 1;
    hal->hw->misc.cs0_dis = 0;
    hal->hw->dma_int_ena.dma_seg_trans_done = 1;
    hal->hw->user.usr_conf_nxt = 1;
    spi_hal_user_start(hal);
}

static esp_err_t IRAM_ATTR spi_device_polling_start_(spi_device_handle_t handle, spi_transaction_t *trans_desc, TickType_t ticks_to_wait)
{
    esp_err_t ret;
    SPI_CHECK(ticks_to_wait == portMAX_DELAY, "currently timeout is not available for polling transactions", ESP_ERR_INVALID_ARG);
    ret = check_trans_valid(handle, trans_desc);
    if (ret!=ESP_OK) return ret;

    spi_host_t *host = handle->host;
    spi_trans_priv_t priv_polling_trans;
    ret = setup_priv_desc(trans_desc, &priv_polling_trans, (host->bus_attr->dma_enabled));
    if (ret!=ESP_OK) return ret;

    /* If device_acquiring_lock is set to handle, it means that the user has already
     * acquired the bus thanks to the function `spi_device_acquire_bus()`.
     * In that case, we don't need to take the lock again. */
    if (host->device_acquiring_lock != handle) {
        /* The user cannot ask for the CS to keep active has the bus is not locked/acquired. */
        if ((trans_desc->flags & SPI_TRANS_CS_KEEP_ACTIVE) != 0) {
            ret = ESP_ERR_INVALID_ARG;
        } else {
            ret = spi_bus_lock_acquire_start(handle->dev_lock, ticks_to_wait);
        }
    } else {
        ret = spi_bus_lock_wait_bg_done(handle->dev_lock, ticks_to_wait);
    }
    if (ret != ESP_OK) {
        uninstall_priv_desc(&priv_polling_trans);
        ESP_LOGE(SPI_TAG, "polling can't get buslock");
        return ret;
    }
    //After holding the buslock, common resource can be accessed !!

    //Polling, no interrupt is used.
    host->polling = true;
    host->cur_trans_buf = priv_polling_trans;

    ESP_LOGV(SPI_TAG, "polling trans");
    spi_new_trans(handle, &host->cur_trans_buf);

    return ESP_OK;
}

/******************************************************************************/

extern int spicommon_irqsource_for_host(spi_host_device_t host);

static spi_device_handle_t spi_device;

uint8_t* framebuffer[FB_UHEIGHT];
#define LLSIZE 12
#define PIXEL_CLOCK 16000000 // 86.43 Hz
#define HSYNC_OFF 40
//#define PIXEL_CLOCK 11428571 // 61.74 Hz
//#define HSYNC_OFF 38 //((int)(PIXEL_CLOCK*0.0000025))

#define init_fb(buf, h) for (int y = 0; y < h; y++) {\
        for (int x = 0; x < 32; x++) buf[FB_WIDTH*y + x] = 0;\
        for (int x = 32; x < FB_WIDTH; x++) buf[FB_WIDTH*y + x] = 1;\
    }
#define init_link(n) lldesc_setup_link(&llchunks[n][1], framebuffer_chunk##n + HSYNC_OFF, sizeof(framebuffer_chunk##n) - HSYNC_OFF, 0);\
    llchunks[n][0] = (lldesc_t){.size = LLSIZE, .length = LLSIZE, .buf = (uint8_t*)dma_conf, .eof = 0, .sosf = 0, .owner = 1, .qe.stqe_next = &llchunks[n][1]};\
    found = false;\
    for (int i = 1; i < 12; i++) {\
        if (llchunks[n][i].eof) {\
            llchunks[n][i].eof = 0;\
            llchunks[n][i].qe.stqe_next = &llchunks[n+1][0];\
            found = true;\
            break;\
        }\
    }\
    if (!found) {ESP_LOGE(TAG, "Could not link chunk " #n); return ESP_ERR_INVALID_STATE;}\
    ESP_LOGV(TAG, "Descriptor " #n " is at %08x, %08x", (uintptr_t)llchunks[n], (uintptr_t)&llchunks[n][1]);

static IRAM_ATTR void link_trans(spi_transaction_t *trans) {
    ESP_LOGV(TAG, "starting transfer");
    spi_ll_clear_int_stat(spi_device->host->hal.hw);
}

static IRAM_ATTR void trans_done(spi_transaction_t* trans) {
    ESP_LOGD(TAG, "transfer completed: %08lx", spi_device->host->hal.hw->dma_int_raw.val);
    spi_ll_clear_intr(spi_device->host->hal.hw, SPI_DMA_SEG_TRANS_DONE_INT_RAW);
}

static int transfer_count = 0;
static IRAM_ATTR void dma_isr(void*) {
    if (spi_device->host->hal.hw->dma_int_raw.val == SPI_DMA_SEG_TRANS_DONE_INT_RAW) {
        transfer_count++;
    } else {
        ESP_DRAM_LOGD(TAG, "DMA interrupt caught after %d transfers: %08lx", transfer_count, spi_device->host->hal.hw->dma_int_raw.val);
        ESP_DRAM_LOGD(TAG, "DMA conf 0x%08lx, GDMA addr 0x%08lx, 0x%08lx", spi_device->host->hal.hw->dma_conf.val, GDMA.channel[spi_device->host->hal.tx_dma_chan].out.dscr_bf0, GDMA.channel[spi_device->host->hal.tx_dma_chan].out.dscr);
        ESP_DRAM_LOGD(TAG, "GDMA byte numbers: %lu, %lu, %lu",
            GDMA.channel[spi_device->host->hal.tx_dma_chan].out.outfifo_status.outfifo_cnt_l1,
            GDMA.channel[spi_device->host->hal.tx_dma_chan].out.outfifo_status.outfifo_cnt_l2,
            GDMA.channel[spi_device->host->hal.tx_dma_chan].out.outfifo_status.outfifo_cnt_l3);
        ESP_DRAM_LOGD(TAG, "Transfer size: %lu", spi_device->host->hal.hw->ms_dlen.ms_data_bitlen);
        spi_ll_clear_int_stat(spi_device->host->hal.hw);
    }
    spi_ll_clear_intr(spi_device->host->hal.hw, SPI_LL_INTR_SEG_DONE);
}

esp_err_t vga_init(void) {
    esp_err_t err;

    // The DMA driver can only send 32k at a time, but we need to send 185k. How fix?
    // The concept is to use multiple chained DMA transfers, using the CONF
    // buffer feature to split up the large data buffer into multiple transfers.
    // This is because the maximum size of any single transfer is 32kB (256kb),
    // so by splitting it up into multiple transfers which trigger each other,
    // we can achieve larger and faster transfers.
    // To save on CPU time, we also make the transfer loop back to itself, which
    // makes the framebuffer essentially act as an infinite transfer.
    // Also, to trigger Vsync, we'll have the first and last transfers in the
    // loop trigger an interrupt, which will deassert and assert the Vsync line,
    // respectively.

    // To set this up without rewriting the entire driver and HAL, we'll abuse
    // some internals of the driver to get the raw data we need without having
    // to generate it all manually. We'll start by creating linked lists for the
    // non-first chunks. We'll then initiate a transfer for the first chunk, but
    // in the pre-transfer callback, we'll link the rest of the generated chunks
    // onto the end of the generated transfer list. We'll also set SPI_USER_CONF
    // to enable configure-segmented transfers. After that, we should be good to
    // let it run.

    ESP_LOGI(TAG, "Initializing vga module");

    // Initialize framebuffer pointers
    for (int i = 0; i < 67; i++) framebuffer[i] = &framebuffer_chunk1[80 + FB_WIDTH*(i+1)];
    for (int i = 0; i < 68; i++) framebuffer[i+67] = &framebuffer_chunk2[80 + FB_WIDTH*i];
    for (int i = 0; i < 68; i++) framebuffer[i+135] = &framebuffer_chunk3[80 + FB_WIDTH*i];
    for (int i = 0; i < 68; i++) framebuffer[i+203] = &framebuffer_chunk4[80 + FB_WIDTH*i];
    for (int i = 0; i < 68; i++) framebuffer[i+271] = &framebuffer_chunk5[80 + FB_WIDTH*i];
    for (int i = 0; i < 61; i++) framebuffer[i+339] = &framebuffer_chunk6[80 + FB_WIDTH*i];
    // Clear screen & initialize Hsync bits
    init_fb(framebuffer_vsync, 3);
    init_fb(framebuffer_chunk1, 68);
    init_fb(framebuffer_chunk2, 68);
    init_fb(framebuffer_chunk3, 68);
    init_fb(framebuffer_chunk4, 68);
    init_fb(framebuffer_chunk5, 68);
    init_fb(framebuffer_chunk6, 68);
    init_fb(framebuffer_chunk7, 34);
    // Initialize config words
    dma_conf[0] = 0xA00000C0UL;
    dma_conf[1] = (sizeof(framebuffer_chunk1) - HSYNC_OFF) * 8 - 1;
    dma_conf_last[0] = 0xA00000C0UL;
    dma_conf_last[1] = (sizeof(framebuffer_chunk7) - HSYNC_OFF) * 8 - 1;
    dma_conf_vsync[0] = 0xA00000C0UL;
    dma_conf_vsync[1] = (sizeof(framebuffer_vsync) - HSYNC_OFF) * 8 - 1;
    // Set up linked lists for next descriptors
    bool found;
    lldesc_setup_link(&llchunks[0][1], framebuffer_vsync + HSYNC_OFF, sizeof(framebuffer_vsync) - HSYNC_OFF, 0);
    llchunks[0][0] = (lldesc_t){.size = LLSIZE, .length = LLSIZE, .buf = (uint8_t*)dma_conf_vsync, .eof = 0, .sosf = 0, .owner = 1, .qe.stqe_next = &llchunks[0][1]};
    found = false;
    for (int i = 1; i < 12; i++) {
        if (llchunks[0][i].eof) {
            llchunks[0][i].eof = 0;
            llchunks[0][i].qe.stqe_next = &llchunks[1][0];
            found = true;
            break;
        }
    }
    if (!found) {ESP_LOGE(TAG, "Could not link chunk vsync"); return ESP_ERR_INVALID_STATE;}
    ESP_LOGV(TAG, "Descriptor vsync is at %08x", (uintptr_t)llchunks[0]);
    init_link(1);
    init_link(2);
    init_link(3);
    init_link(4);
    init_link(5);
    init_link(6);
    lldesc_setup_link(&llchunks[7][1], framebuffer_chunk7 + HSYNC_OFF, sizeof(framebuffer_chunk7) - HSYNC_OFF, 0);
    llchunks[7][0] = (lldesc_t){.size = LLSIZE, .length = LLSIZE, .buf = (uint8_t*)dma_conf_last, .eof = 0, .sosf = 0, .owner = 1, .qe.stqe_next = &llchunks[7][1]};
    found = false;
    for (int i = 1; i < 12; i++) {
        if (llchunks[7][i].eof) {
            llchunks[7][i].eof = 0;
            llchunks[7][i].qe.stqe_next = llchunks[0];
            found = true;
            break;
        }
    }
    if (!found) {ESP_LOGE(TAG, "Could not link chunk 7"); return ESP_ERR_INVALID_STATE;}

    // Initialize SPI host
    ESP_LOGV(TAG, "Initializing SPI bus");
    spi_bus_config_t host_conf;
    host_conf.data0_io_num = host_conf.mosi_io_num = GPIO_NUM_8;
    host_conf.data1_io_num = host_conf.miso_io_num = GPIO_NUM_3;
    host_conf.data2_io_num = host_conf.quadwp_io_num = GPIO_NUM_9;
    host_conf.data3_io_num = host_conf.quadhd_io_num = GPIO_NUM_10;
    host_conf.data4_io_num = GPIO_NUM_11;
    host_conf.data5_io_num = GPIO_NUM_12;
    host_conf.data6_io_num = GPIO_NUM_13;
    host_conf.data7_io_num = GPIO_NUM_14;
    host_conf.sclk_io_num = GPIO_NUM_47;
    host_conf.max_transfer_sz = 32768;
    host_conf.flags = SPICOMMON_BUSFLAG_OCTAL | SPICOMMON_BUSFLAG_GPIO_PINS | SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_SCLK;
    host_conf.intr_flags = 0;
    host_conf.isr_cpu_id = INTR_CPU_ID_AUTO;
    CHECK_CALLE(spi_bus_initialize(SPI2_HOST, &host_conf, SPI_DMA_CH_AUTO), "Could not initialize SPI bus");

    gpio_set_drive_capability(GPIO_NUM_8, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability(GPIO_NUM_3, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability(GPIO_NUM_9, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability(GPIO_NUM_10, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability(GPIO_NUM_47, GPIO_DRIVE_CAP_3);

    // Initialize device
    ESP_LOGV(TAG, "Initializing SPI device");
    spi_device_interface_config_t device_conf;
    device_conf.command_bits = 0;
    device_conf.address_bits = 0;
    device_conf.dummy_bits = 0;
    device_conf.mode = 0;
    device_conf.clock_source = SPI_CLK_SRC_APB;
    device_conf.clock_speed_hz = PIXEL_CLOCK;
    device_conf.duty_cycle_pos = 0;
    device_conf.cs_ena_pretrans = device_conf.cs_ena_posttrans = 0;
    device_conf.spics_io_num = GPIO_NUM_18;
    device_conf.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
    device_conf.queue_size = 2;
    device_conf.pre_cb = link_trans;
    device_conf.post_cb = trans_done;
    CHECK_CALLE(spi_bus_add_device(SPI2_HOST, &device_conf, &spi_device), "Could not initialize SPI device");

    // Dispatch transaction; link_trans will finish it off
    ESP_LOGV(TAG, "Starting initial transfer");
    spi_transaction_t trans;
    trans.flags = SPI_TRANS_MODE_OCT;
    trans.tx_buffer = framebuffer_vsync + HSYNC_OFF; trans.length = (sizeof(framebuffer_vsync) - HSYNC_OFF)*8;
    trans.rx_buffer = NULL; trans.rxlength = 0;
    //CHECK_CALLE(spi_device_acquire_bus(spi_device, portMAX_DELAY), "Could not acquire SPI bus");
    //CHECK_CALLE(spi_device_queue_trans(spi_device, &trans, portMAX_DELAY), "Could not start SPI transfer");
    intr_handle_t intr;
    esp_intr_free(spi_device->host->intr);
    spi_ll_clear_int_stat(spi_device->host->hal.hw);
    esp_intr_alloc(ETS_SPI2_INTR_SOURCE, 0, dma_isr, NULL, &intr);
    esp_intr_enable(intr);
    CHECK_CALLE(spi_device_polling_start_(spi_device, &trans, portMAX_DELAY), "Could not start SPI transfer");
    //CHECK_CALLE(spi_device_polling_end(spi_device, portMAX_DELAY), "stop");
    //ESP_LOGD(TAG, "transfer stopped: %08lx", spi_device->host->hal.hw->dma_int_raw.val);

    esp_register_shutdown_handler(vga_deinit);
    ESP_LOGV(TAG, "SPI initialization complete");
    return ESP_OK;
}

void vga_deinit(void) {
    // Disconnect the looping DMA transfer to cause the transfer to finish
    ESP_LOGV(TAG, "Deinitializing VGA, this may or may not work");
    spi_hal_context_t* hal = &spi_device->host->hal;
    for (int i = 0; i < 12; i++) {
        for (int j = 0; j < 12; j++) {
            llchunks[i][j].eof = 1;
            llchunks[i][j].qe.stqe_next = NULL;
        }
    }
    // Wait for the transfer to end
    /*if (spi_device_polling_end(spi_device, pdMS_TO_TICKS(5000)) == ESP_OK) {
        spi_bus_remove_device(spi_device);
        spi_bus_free(SPI2_HOST);
    }*/
}
