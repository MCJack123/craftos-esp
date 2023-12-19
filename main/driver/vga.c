#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <hal/spi_hal.h>
#include <hal/spi_ll.h>
#include <hal/gdma_ll.h>
#include <soc/lldesc.h>
#include <soc/gdma_struct.h>
#include <soc/io_mux_reg.h>
#include <freertos/queue.h>
#include "vga.h"

static const char* TAG = "vga";

/* from spi_master.c; Apache 2.0 */

struct spi_bus_lock_t;
struct spi_bus_lock_dev_t;
/// Handle to the lock of an SPI bus
typedef struct spi_bus_lock_t* spi_bus_lock_handle_t;
/// Handle to lock of one of the device on an SPI bus
typedef struct spi_bus_lock_dev_t* spi_bus_lock_dev_handle_t;

/// Background operation control function
typedef void (*bg_ctrl_func_t)(void*);

typedef struct lldesc_s lldesc_t;

/// Attributes of an SPI bus
typedef struct {
    spi_bus_config_t bus_cfg;   ///< Config used to initialize the bus
    uint32_t flags;             ///< Flags (attributes) of the bus
    int max_transfer_sz;        ///< Maximum length of bytes available to send
    bool dma_enabled;           ///< To enable DMA or not
    int tx_dma_chan;            ///< TX DMA channel, on ESP32 and ESP32S2, tx_dma_chan and rx_dma_chan are same
    int rx_dma_chan;            ///< RX DMA channel, on ESP32 and ESP32S2, tx_dma_chan and rx_dma_chan are same
    int dma_desc_num;           ///< DMA descriptor number of dmadesc_tx or dmadesc_rx.
    lldesc_t *dmadesc_tx;       ///< DMA descriptor array for TX
    lldesc_t *dmadesc_rx;       ///< DMA descriptor array for RX
    spi_bus_lock_handle_t lock;
#ifdef CONFIG_PM_ENABLE
    esp_pm_lock_handle_t pm_lock;   ///< Power management lock
#endif
} spi_bus_attr_t;

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

/******************************************************************************/

extern int spicommon_irqsource_for_host(spi_host_device_t host);

static spi_device_handle_t spi_device;
// framebuffer transfer is split into multiple chunks
// LLDESC_MAX_NUM_PER_DESC represents CONF descriptor; this is to make the lldesc
// allocator split the buffer right at the CONF descriptor
// uses more memory, but oh well -\_(*-*)_/-
DMA_ATTR uint8_t framebuffer_vsync[LLDESC_MAX_NUM_PER_DESC + FB_WIDTH*3]; // config sets DLEN, INT_ENA; 3/445 lines
DMA_ATTR uint8_t framebuffer_chunk1[LLDESC_MAX_NUM_PER_DESC + FB_WIDTH*68]; // config sets DLEN, INT_ENA; 71/445 lines
DMA_ATTR uint8_t framebuffer_chunk2[LLDESC_MAX_NUM_PER_DESC + FB_WIDTH*68]; // config sets DLEN; 139/445 lines
DMA_ATTR uint8_t framebuffer_chunk3[LLDESC_MAX_NUM_PER_DESC + FB_WIDTH*68]; // config sets DLEN; 207/445 lines
DMA_ATTR uint8_t framebuffer_chunk4[LLDESC_MAX_NUM_PER_DESC + FB_WIDTH*68]; // config sets DLEN; 275/445 lines
DMA_ATTR uint8_t framebuffer_chunk5[LLDESC_MAX_NUM_PER_DESC + FB_WIDTH*68]; // config sets DLEN; 343/445 lines
DMA_ATTR uint8_t framebuffer_chunk6[LLDESC_MAX_NUM_PER_DESC + FB_WIDTH*68]; // config sets DLEN; 411/445 lines
DMA_ATTR uint8_t framebuffer_chunk7[LLDESC_MAX_NUM_PER_DESC + FB_WIDTH*34]; // config sets DLEN, INT_ENA; 445/445 lines
DMA_ATTR lldesc_t llchunks[8][7];

uint8_t* framebuffer[FB_UHEIGHT];

#define init_fb(buf, h) for (int y = 0; y < h; y++) {\
        for (int x = 0; x < 64; x++) buf[LLDESC_MAX_NUM_PER_DESC + FB_WIDTH*y + x] = 0;\
        for (int x = 64; x < FB_WIDTH; x++) buf[LLDESC_MAX_NUM_PER_DESC + FB_WIDTH*y + x] = 1;\
    }
#define init_cfg(buf, a) ((uint32_t*)buf)[0] = (1 << 6) | (a << 7) | (0xA << 28);\
                         ((uint32_t*)buf)[1] = (sizeof(buf) - LLDESC_MAX_NUM_PER_DESC) * 8
#define init_link(n) lldesc_setup_link(llchunks[n-1], framebuffer_chunk##n, sizeof(framebuffer_chunk##n), 0);\
    for (int i = 0; i < 8; i++) {\
        if (llchunks[n-1][i].eof) {\
            llchunks[n-1][i].eof = 0;\
            llchunks[n-1][i].qe.stqe_next = llchunks[n];\
        }\
    }

static void link_trans(spi_transaction_t *trans) {
    ESP_LOGV(TAG, "Preflight link start");
    spi_hal_context_t* hal = &spi_device->host->hal;
    // Set interrupt flags for chunks that need it
    ((uint32_t*)framebuffer_vsync)[2] = hal->hw->misc.val & ~SPI_CS0_DIS;
    ((uint32_t*)framebuffer_chunk1)[2] = hal->hw->misc.val | SPI_CS0_DIS;
    // Find last descriptor
    for (int i = 0; i < hal->dmadesc_n; i++) {
        if (hal->dmadesc_tx[i].eof) {
            ESP_LOGV(TAG, "Found last DMA descriptor; linking full descriptor loop");
            // Link rest of chunks on
            hal->dmadesc_tx[i].eof = 0;
            hal->dmadesc_tx[i].qe.stqe_next = llchunks[0];
            // Link last chunk back to first
            for (int j = 0; j < 8; j++) {
                if (llchunks[6][j].eof) {
                    llchunks[6][j].eof = 0;
                    llchunks[6][j].qe.stqe_next = hal->dmadesc_tx;
                }
            }
            // Set CONF flag and start transfer
            hal->hw->slave.dma_seg_magic_value = 0xA;
            hal->hw->slave.usr_conf = 1;
            hal->hw->user.usr_conf_nxt = 1;
            return;
        }
    }
    ESP_LOGE(TAG, "BUG: Could not find final SPI TX descriptor! VGA will not work.");
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

    ESP_LOGD(TAG, "Initializing VGA module");

    // Initialize framebuffer pointers
    for (int i = 0; i < 67; i++) framebuffer[i] = &framebuffer_chunk1[LLDESC_MAX_NUM_PER_DESC + 160 + FB_WIDTH*(i+1)];
    for (int i = 0; i < 68; i++) framebuffer[i+67] = &framebuffer_chunk2[LLDESC_MAX_NUM_PER_DESC + 160 + FB_WIDTH*i];
    for (int i = 0; i < 68; i++) framebuffer[i+135] = &framebuffer_chunk3[LLDESC_MAX_NUM_PER_DESC + 160 + FB_WIDTH*i];
    for (int i = 0; i < 68; i++) framebuffer[i+203] = &framebuffer_chunk4[LLDESC_MAX_NUM_PER_DESC + 160 + FB_WIDTH*i];
    for (int i = 0; i < 68; i++) framebuffer[i+271] = &framebuffer_chunk5[LLDESC_MAX_NUM_PER_DESC + 160 + FB_WIDTH*i];
    for (int i = 0; i < 61; i++) framebuffer[i+339] = &framebuffer_chunk6[LLDESC_MAX_NUM_PER_DESC + 160 + FB_WIDTH*i];
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
    init_cfg(framebuffer_vsync, 1);
    init_cfg(framebuffer_chunk1, 1);
    init_cfg(framebuffer_chunk2, 0);
    init_cfg(framebuffer_chunk3, 0);
    init_cfg(framebuffer_chunk4, 0);
    init_cfg(framebuffer_chunk5, 0);
    init_cfg(framebuffer_chunk6, 0);
    init_cfg(framebuffer_chunk7, 0);
    // Set up linked lists for next descriptors
    init_link(1);
    init_link(2);
    init_link(3);
    init_link(4);
    init_link(5);
    init_link(6);
    lldesc_setup_link(llchunks[6], framebuffer_chunk7, sizeof(framebuffer_chunk7), 0); // we link this later

    // Set up Vsync GPIO
    gpio_iomux_out(GPIO_NUM_36, FUNC_GPIO36_GPIO36, false);
    gpio_set_direction(GPIO_NUM_36, GPIO_MODE_INPUT_OUTPUT);

    // Initialize SPI host
    ESP_LOGV(TAG, "Initializing SPI bus");
    spi_bus_config_t host_conf;
    host_conf.data0_io_num = host_conf.mosi_io_num = GPIO_NUM_35;
    host_conf.data1_io_num = host_conf.miso_io_num = GPIO_NUM_37;
    host_conf.data2_io_num = host_conf.quadwp_io_num = GPIO_NUM_38;
    host_conf.data3_io_num = host_conf.quadhd_io_num = GPIO_NUM_9;
    host_conf.data4_io_num = GPIO_NUM_10;
    host_conf.data5_io_num = GPIO_NUM_11;
    host_conf.data6_io_num = GPIO_NUM_12;
    host_conf.data7_io_num = GPIO_NUM_13;
    host_conf.sclk_io_num = -1;
    host_conf.max_transfer_sz = 32768;
    host_conf.flags = SPICOMMON_BUSFLAG_OCTAL | SPICOMMON_BUSFLAG_IOMUX_PINS | SPICOMMON_BUSFLAG_MASTER;
    host_conf.intr_flags = 0;
    host_conf.isr_cpu_id = INTR_CPU_ID_0;
    CHECK_CALLE(spi_bus_initialize(SPI2_HOST, &host_conf, SPI_DMA_CH_AUTO), "Could not initialize SPI bus");

    // Initialize device
    ESP_LOGV(TAG, "Initializing SPI device");
    spi_device_interface_config_t device_conf;
    device_conf.command_bits = 0;
    device_conf.address_bits = 0;
    device_conf.dummy_bits = 0;
    device_conf.mode = 0;
    device_conf.clock_source = SPI_CLK_SRC_APB;
    device_conf.clock_speed_hz = 15750000; // will likely be adjusted to 16000000
    device_conf.duty_cycle_pos = 0;
    device_conf.cs_ena_pretrans = device_conf.cs_ena_posttrans = 0;
    device_conf.spics_io_num = 36;
    device_conf.flags = SPI_DEVICE_HALFDUPLEX;
    device_conf.queue_size = 2;
    device_conf.pre_cb = link_trans;
    device_conf.post_cb = NULL;
    CHECK_CALLE(spi_bus_add_device(SPI2_HOST, &device_conf, &spi_device), "Could not initialize SPI device");

    // Dispatch transaction; link_trans will finish it off
    ESP_LOGV(TAG, "Starting initial transfer");
    spi_transaction_t trans;
    trans.flags = SPI_TRANS_MODE_OCT;
    trans.tx_buffer = framebuffer_vsync; trans.length = sizeof(framebuffer_vsync)*8;
    trans.rx_buffer = NULL; trans.rxlength = 0;
    //CHECK_CALLE(spi_device_acquire_bus(spi_device, portMAX_DELAY), "Could not acquire SPI bus");
    //CHECK_CALLE(spi_device_queue_trans(spi_device, &trans, portMAX_DELAY), "Could not start SPI transfer");
    CHECK_CALLE(spi_device_polling_start(spi_device, &trans, portMAX_DELAY), "Could not start SPI transfer");

    esp_register_shutdown_handler(vga_deinit);
    ESP_LOGV(TAG, "SPI initialization complete");
    return ESP_OK;
}

void vga_deinit(void) {
    // Disconnect the looping DMA transfer to cause the transfer to finish
    ESP_LOGV(TAG, "Deinitializing VGA, this may or may not work");
    spi_hal_context_t* hal = &spi_device->host->hal;
    for (int j = 0; j < 8; j++) {
        if (llchunks[6][j].qe.stqe_next == hal->dmadesc_tx) {
            llchunks[6][j].eof = 1;
            llchunks[6][j].qe.stqe_next = NULL;
        }
    }
    // Wait for the transfer to end
    spi_device_polling_end(spi_device, 1000 / portTICK_PERIOD_MS);
    spi_bus_remove_device(spi_device);
    spi_bus_free(SPI2_HOST);
}
