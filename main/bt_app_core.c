/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "freertos/xtensa_api.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bt_app_core.h"
#include "driver/i2s.h"
#include "freertos/ringbuf.h"
#include "bt_app_av.h"

static AudioRmsCB rmsCB = NULL;

void setAudioRmsCB(AudioRmsCB cb) {
  rmsCB = cb;
}

void notifyAudioRMS(float left, float right) {
  if (rmsCB) {
    rmsCB(left, right);
  }
}

static const int32_t volumeScale[128] = { //0..127 mapped to 0..65536 in square
        0,    4,   16,   37,   65,  102,  146,  199,
      260,  329,  406,  492,  585,  687,  796,  914,
     1040, 1174, 1316, 1467, 1625, 1792, 1967, 2149,
     2340, 2540, 2747, 2962, 3186, 3417, 3657, 3905,
     4161, 4425, 4697, 4977, 5266, 5563, 5867, 6180,
     6501, 6830, 7168, 7513, 7866, 8228, 8598, 8976,
     9362, 9756,10158,10568,10987,11414,11848,12291,
    12742,13201,13669,14144,14628,15119,15619,16127,
    16643,17167,17699,18240,18788,19345,19910,20483,
    21064,21653,22250,22856,23469,24091,24721,25359,
    26005,26659,27321,27992,28670,29357,30052,30755,
    31466,32185,32912,33648,34391,35143,35903,36671,
    37447,38231,39023,39824,40632,41449,42274,43107,
    43948,44797,45655,46520,47394,48275,49165,50063,
    50969,51884,52806,53736,54675,55622,56577,57540,
    58511,59490,60477,61473,62476,63488,64508,65536 
};


/*******************************
 * STATIC FUNCTION DECLARATIONS
 ******************************/

/* handler for application task */
static void bt_app_task_handler(void *arg);
/* handler for I2S task */
static void bt_i2s_task_handler(void *arg);
/* message sender */
static bool bt_app_send_msg(bt_app_msg_t *msg);
/* handle dispatched messages */
static void bt_app_work_dispatched(bt_app_msg_t *msg);

/*******************************
 * STATIC VARIABLE DEFINITIONS
 ******************************/

static xQueueHandle s_bt_app_task_queue = NULL;  /* handle of work queue */
static xTaskHandle s_bt_app_task_handle = NULL;  /* handle of application task  */
static xTaskHandle s_bt_i2s_task_handle = NULL;  /* handle of I2S task */
static RingbufHandle_t s_ringbuf_i2s = NULL;     /* handle of ringbuffer for I2S */

/*******************************
 * STATIC FUNCTION DEFINITIONS
 ******************************/

static bool bt_app_send_msg(bt_app_msg_t *msg)
{
    if (msg == NULL) {
        return false;
    }

    if (xQueueSend(s_bt_app_task_queue, msg, 10 / portTICK_RATE_MS) != pdTRUE) {
        ESP_LOGE(BT_APP_CORE_TAG, "%s xQueue send failed", __func__);
        return false;
    }
    return true;
}

static void bt_app_work_dispatched(bt_app_msg_t *msg)
{
    if (msg->cb) {
        msg->cb(msg->event, msg->param);
    }
}

static void bt_app_task_handler(void *arg)
{
    bt_app_msg_t msg;

    for (;;) {
        /* receive message from work queue and handle it */
        if (pdTRUE == xQueueReceive(s_bt_app_task_queue, &msg, (portTickType)portMAX_DELAY)) {
            ESP_LOGD(BT_APP_CORE_TAG, "%s, signal: 0x%x, event: 0x%x", __func__, msg.sig, msg.event);

            switch (msg.sig) {
            case BT_APP_SIG_WORK_DISPATCH:
                bt_app_work_dispatched(&msg);
                break;
            default:
                ESP_LOGW(BT_APP_CORE_TAG, "%s, unhandled signal: %d", __func__, msg.sig);
                break;
            } /* switch (msg.sig) */

            if (msg.param) {
                free(msg.param);
            }
        }
    }
}

static void bt_i2s_task_handler(void *arg)
{
    uint8_t *data = NULL;
    size_t item_size = 0;
    size_t bytes_written = 0;
    static float leftSquares = 0;
    static float rightSquares = 0;
    static int sampleCount = 0;

    for (;;) {
        /* receive data from ringbuffer and write it to I2S DMA transmit buffer */
        data = (uint8_t *)xRingbufferReceive(s_ringbuf_i2s, &item_size, (portTickType)portMAX_DELAY);
        if (item_size != 0){
            int16_t *buf = (int16_t*)data;
            int numSamples = item_size / 2;
            uint8_t vol = getVolume();
            float volScale = volumeScale[vol] / 65536.0f;
            // Sample processing can go here. Right now, only volume scaling and RMS analysis
            for (int i=0; i<numSamples; i += 2) {
                float l = (float)buf[i];
                l *= volScale;
                leftSquares += l*l;
                buf[i] = l;
                float r = (float)buf[i+1];
                r *= volScale;
                rightSquares += r*r;
                buf[i+1] = r;
            }
            sampleCount += numSamples;
            i2s_write(0, data, item_size, &bytes_written, portMAX_DELAY);
            vRingbufferReturnItem(s_ringbuf_i2s, (void *)data);
            if (sampleCount >= 1500) {
              notifyAudioRMS(sqrtf(leftSquares / sampleCount), sqrtf(rightSquares / sampleCount));
              leftSquares = 0;
              rightSquares = 0;
              sampleCount = 0;
            }
        }
    }
}

/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/

bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback)
{
    ESP_LOGD(BT_APP_CORE_TAG, "%s event: 0x%x, param len: %d", __func__, event, param_len);

    bt_app_msg_t msg;
    memset(&msg, 0, sizeof(bt_app_msg_t));

    msg.sig = BT_APP_SIG_WORK_DISPATCH;
    msg.event = event;
    msg.cb = p_cback;

    if (param_len == 0) {
        return bt_app_send_msg(&msg);
    } else if (p_params && param_len > 0) {
        if ((msg.param = malloc(param_len)) != NULL) {
            memcpy(msg.param, p_params, param_len);
            /* check if caller has provided a copy callback to do the deep copy */
            if (p_copy_cback) {
                p_copy_cback(msg.param, p_params, param_len);
            }
            return bt_app_send_msg(&msg);
        }
    }

    return false;
}

void bt_app_task_start_up(void)
{
    s_bt_app_task_queue = xQueueCreate(10, sizeof(bt_app_msg_t));
    xTaskCreate(bt_app_task_handler, "BtAppTask", 3072, NULL, configMAX_PRIORITIES - 3, &s_bt_app_task_handle);
}

void bt_app_task_shut_down(void)
{
    if (s_bt_app_task_handle) {
        vTaskDelete(s_bt_app_task_handle);
        s_bt_app_task_handle = NULL;
    }
    if (s_bt_app_task_queue) {
        vQueueDelete(s_bt_app_task_queue);
        s_bt_app_task_queue = NULL;
    }
}

void bt_i2s_task_start_up(void)
{
    if ((s_ringbuf_i2s = xRingbufferCreate(8 * 1024, RINGBUF_TYPE_BYTEBUF)) == NULL) {
        return;
    }
    xTaskCreate(bt_i2s_task_handler, "BtI2STask", 1024, NULL, configMAX_PRIORITIES - 3, &s_bt_i2s_task_handle);
}

void bt_i2s_task_shut_down(void)
{
    if (s_bt_i2s_task_handle) {
        vTaskDelete(s_bt_i2s_task_handle);
        s_bt_i2s_task_handle = NULL;
    }
    if (s_ringbuf_i2s) {
        vRingbufferDelete(s_ringbuf_i2s);
        s_ringbuf_i2s = NULL;
    }
}

size_t write_ringbuf(const uint8_t *data, size_t size)
{
    BaseType_t done = xRingbufferSend(s_ringbuf_i2s, (void *)data, size, (portTickType)portMAX_DELAY);

    return done ? size : 0;
}
