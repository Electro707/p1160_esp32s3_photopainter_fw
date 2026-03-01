
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "eink.h"
#include "driver/spi_master.h"
#include "main.h"
#include "common.h"
#include "fileSys.h"
#include "ff.h"

#define RESET_DISPLAY() gpio_set_level(IO_DISP_RST, 0)
#define UNRST_DISPLAY() gpio_set_level(IO_DISP_RST, 1)

#define CS_SELECT() gpio_set_level(IO_DISP_CS, 0)
#define CS_RELEASE() gpio_set_level(IO_DISP_CS, 1)

#define SET_DC_CMD() gpio_set_level(IO_DISP_DC, 0)
#define SET_DC_DAT() gpio_set_level(IO_DISP_DC, 1)

#define READ_BUSY() gpio_get_level(IO_DISP_BUSY)

// because of the SPI's 18-bit limitation in size, split this up so each
// transaction takes 32kB (256kb) of data, which is split across 6 transactions,
// making up the (800*480*4)/8 bytes required
// this is defined as 80 pixels tall by the display's width
#define TRANSFER_SIZE_BYTES     32000
#define TRANSFER_SIZE_BITS      (TRANSFER_SIZE_BYTES*8)
#define TRANSFER_N              6

const char *TAG = "eink";

WORD_ALIGNED_ATTR EXT_RAM_BSS_ATTR static u8 dispFrameBuff[DISP_FB_SIZE];
SemaphoreHandle_t displayFbMutex;

// spi transaction settings
static spi_transaction_t spiTransactSett;

// todo: optimize with manually writing to register, this API is way too bloated for what it should be
void spiSendAndWait(u8 toSend){
    spiTransactSett.flags = SPI_TRANS_USE_TXDATA;
    spiTransactSett.length = 8;
    spiTransactSett.tx_data[0] = toSend;
    spi_device_polling_transmit(dispSpi, &spiTransactSett);
}

void dispReset(void){
    UNRST_DISPLAY();
    delayMs(50);
    RESET_DISPLAY();
    delayMs(20);
    UNRST_DISPLAY();
    delayMs(50);
}



void dispWaitBusy(void) {
    // todo: add a timeout
    while(1){
        if(READ_BUSY()) {
            break;
        }
        delayMs(10);
    }
}

// sends the CMD alone to the display
void dispSendCmdOnly(u8 cmd){
    SET_DC_CMD();
    CS_SELECT();
    spiSendAndWait(cmd);
    CS_RELEASE();
}

// begins a frame, sending CMD for some data to be sent
void dispBeginCmd(u8 cmd){
    SET_DC_CMD();
    CS_SELECT();
    spiSendAndWait(cmd);
    SET_DC_DAT();
}


void dispInit(void){
    displayFbMutex = xSemaphoreCreateMutex();

    spi_device_acquire_bus(dispSpi, portMAX_DELAY);
}

void dispBoot(void){
    memset(&spiTransactSett, 0 ,sizeof(spiTransactSett));
    spiTransactSett.flags = SPI_TRANS_USE_TXDATA;

    dispReset();
    dispWaitBusy();
    delayMs(50);

    // copied from Waveshare's example
    dispBeginCmd(0xAA);     // CMDH
    spiSendAndWait(0x49);
    spiSendAndWait(0x55);
    spiSendAndWait(0x20);
    spiSendAndWait(0x08);
    spiSendAndWait(0x09);
    spiSendAndWait(0x18);
    CS_RELEASE();

    dispBeginCmd(0x01);     //
    spiSendAndWait(0x3F);
    CS_RELEASE();

    dispBeginCmd(0x00);
    spiSendAndWait(0x5F);
    spiSendAndWait(0x69);
    CS_RELEASE();

    dispBeginCmd(0x03);
    spiSendAndWait(0x00);
    spiSendAndWait(0x54);
    spiSendAndWait(0x00);
    spiSendAndWait(0x44);
    CS_RELEASE();

    dispBeginCmd(0x05);
    spiSendAndWait(0x40);
    spiSendAndWait(0x1F);
    spiSendAndWait(0x1F);
    spiSendAndWait(0x2C);
    CS_RELEASE();

    dispBeginCmd(0x06);
    spiSendAndWait(0x6F);
    spiSendAndWait(0x1F);
    spiSendAndWait(0x17);
    spiSendAndWait(0x49);
    CS_RELEASE();

    dispBeginCmd(0x08);
    spiSendAndWait(0x6F);
    spiSendAndWait(0x1F);
    spiSendAndWait(0x1F);
    spiSendAndWait(0x22);
    CS_RELEASE();

    dispBeginCmd(0x30);
    spiSendAndWait(0x03);
    CS_RELEASE();

    dispBeginCmd(0x50);
    spiSendAndWait(0x3F);
    CS_RELEASE();

    dispBeginCmd(0x60);
    spiSendAndWait(0x02);
    spiSendAndWait(0x00);
    CS_RELEASE();

    dispBeginCmd(0x61);         // <- this seems like the cmd to set the frame pointer? or size, not sure!
    spiSendAndWait(0x03);
    spiSendAndWait(0x20);
    spiSendAndWait(0x01);
    spiSendAndWait(0xE0);
    CS_RELEASE();

    dispBeginCmd(0x84);
    spiSendAndWait(0x01);
    CS_RELEASE();

    dispBeginCmd(0xE3);
    spiSendAndWait(0x2F);
    CS_RELEASE();

    dispSendCmdOnly(0x04);  //PWR on
    dispWaitBusy();         //waiting for the electronic paper IC to release the idle signal
}

void dispFillColor(dispColor_e color){
    u8 colorM = (color << 4) | color;
    u8 *fb = takeDispFb(portMAX_DELAY);
    if(fb != NULL){
        memset(dispFrameBuff, colorM, sizeof(dispFrameBuff));
        releaseDispFb();
    }
}

void dispCheckerPattern(u32 checkerSizeLog2){
    u32 color;
    u32 pixelIdx = 0;
    u8 *fb = takeDispFb(portMAX_DELAY);
    if(fb == NULL){
        return;
    }
    for(u32 y=0;y<DISPLAY_H;y++){
        for(u32 x=0;x<DISPLAY_W;x++){
            color = (x >> checkerSizeLog2) + (y >> checkerSizeLog2);
            // take into account that orange (4) is missing
            color %= 6;
            if(color > EPD_COLOR_RED) color++;
            color &= 0x0F;
            // if we are on the even pixels, shift by 4 and OR with previous color. Otherwise just set the 4-bit color
            if((pixelIdx & 1) == 0){  
                fb[pixelIdx >> 1] = color;
            } else {
                fb[pixelIdx >> 1] |= (color << 4);
            }
            
            pixelIdx++;
        }
    }
    releaseDispFb();
}

int setFrameBuffRaw(u8 *data, u32 len, u32 offset){
    if((offset + len) > DISP_FB_SIZE){
        return -1;
    }
    u8 *fb = dispFrameBuff+offset;
    memcpy(fb, data, len);
    return 0;
}

void dispUpdate(void){
    u8 *dat = takeDispFb(portMAX_DELAY);
    if(dat == NULL){
        return;
    }
    dispBeginCmd(0x10);
    spiTransactSett.flags = SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL | SPI_TRANS_DMA_USE_PSRAM;
    spiTransactSett.length = TRANSFER_SIZE_BITS;
    for(u32 n=0; n < TRANSFER_N; n++){
        spiTransactSett.tx_buffer = dat;
        spi_device_polling_transmit(dispSpi, &spiTransactSett);
        dat += TRANSFER_SIZE_BYTES;
    }
    CS_RELEASE();
    dispWaitBusy();
    releaseDispFb();

    dispBeginCmd(0x12);
    spiSendAndWait(0x00);
    CS_RELEASE();
    dispWaitBusy();
}

u8* takeDispFb(TickType_t timeout){
    u32 stat = xSemaphoreTake(displayFbMutex, timeout);
    if(stat == pdFALSE){
        return NULL;
    }
    return dispFrameBuff;
}

void releaseDispFb(void){
    xSemaphoreGive(displayFbMutex);
}