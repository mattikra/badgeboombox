/*
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "main.h"
#include "bt_app_av.h"
#include "bt_app_core.h"

#define DBMETER_HEIGHT 5
#define DBMETER_MAX 70
#define DBMETER_MIN 20

/** events types that may come from the audio stack */
typedef enum BTAudioEventType_ {
  Event_StateChanged = 1, ///< audio state has changed, may be queried using getAudioState
  Event_RMSUpdate        ///< audio RMS update
} BTAudioEventType;

/** an audio event struct */
typedef struct BTAudioEvent_ {
  BTAudioEventType type;
  union {
    struct {
      float left;
      float right;
    } rms;
  } data;
} BTAudioEvent;

void bt_init(void);
void drawAll();
void drawDBMeter();

static pax_buf_t screenBuf;
static xQueueHandle buttonQueue;
static xQueueHandle audioQueue;
static float leftDBMeter = 0;
static float rightDBMeter = 0;
static BTAudioState audioState = {
  .connectionState = ESP_A2D_CONNECTION_STATE_DISCONNECTED,
  .playState = ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND,
  .volume = 100,
  .sampleRate = 16000,
  .title = {0},
  .artist = {0},
  .album = {0}
};

/** callback from bt_app_av: state has changed */
void audioStateChange() {
  BTAudioEvent evt = {
    .type = Event_StateChanged
  };
  xQueueSend(audioQueue, &evt, 0);  //evt is copied to queue
}

/** callback from bt_app_core: new volume measurement */
void audioRMSUpdate(float left, float right) {
  BTAudioEvent evt;
  evt.type = Event_RMSUpdate;
  evt.data.rms.left = left;
  evt.data.rms.right = right;
  xQueueSend(audioQueue, &evt, 0); //evt is copied to queue
}

// Exits the app, returning to the launcher.
void exit_to_launcher() {
    REG_WRITE(RTC_CNTL_STORE0_REG, 0);
    esp_restart();
}

void app_main() {
    // Init the screen, the I2C and the SPI busses.
    bsp_init();
    // Init the RP2040 (responsible for buttons among other important things).
    bsp_rp2040_init();
    // This queue is used to await button presses.
    buttonQueue = get_rp2040()->queue;
    
    // Init graphics for the screen.
    pax_buf_init(&screenBuf, NULL, 320, 240, PAX_BUF_16_565RGB);
    
    // Init NVS.
    nvs_flash_init();
    
    //show something
    drawAll();

    // start Bluetooth
    setAudioStateChangeCB(&audioStateChange);
    setAudioRmsCB(&audioRMSUpdate);
    bt_init();
    
  // this queue receives events from the audio stack
  audioQueue = xQueueCreate( 10, sizeof(BTAudioEvent) );


  //generate queue set for button and audio events
  QueueSetHandle_t queueSet = xQueueCreateSet(20);
  xQueueAddToSet(buttonQueue, queueSet);
  xQueueAddToSet(audioQueue, queueSet);

  while (1) { //handle events from both button and audio queue
    QueueSetMemberHandle_t queue = xQueueSelectFromSet(queueSet, portMAX_DELAY);
    if (queue == buttonQueue) {
      rp2040_input_message_t message;
      xQueueReceive(buttonQueue, &message, 0);
      if (message.state) {
        switch(message.input) {
          case RP2040_INPUT_BUTTON_HOME:
            exit_to_launcher();
            break;
          case RP2040_INPUT_JOYSTICK_UP:
            volume_set_by_local_host(audioState.volume < 122 ? (audioState.volume+5) : 127);
            drawAll();
            break;
          case RP2040_INPUT_JOYSTICK_DOWN:
            volume_set_by_local_host(audioState.volume > 5 ? (audioState.volume-5) : 0);
            drawAll();
            break;

        }
      }
    } else if (queue == audioQueue) { //audio event
      BTAudioEvent evt;
      xQueueReceive(audioQueue, &evt, 0);
      if (evt.type == Event_StateChanged) { //state changed: update main UI
        getAudioState(&audioState);
        drawAll();
      } else if (evt.type == Event_RMSUpdate) {  //RMS: Update bars
        float leftDB = 20 * log10(evt.data.rms.left);
        float rightDB = 20 * log10(evt.data.rms.right);
        leftDBMeter = (leftDB - DBMETER_MIN) / (DBMETER_MAX - DBMETER_MIN);
        rightDBMeter = (rightDB - DBMETER_MIN) / (DBMETER_MAX - DBMETER_MIN);
        drawDBMeter();
      }
    }
  }

}


void drawAll() {
  static const char disconnected[] = "Disconnected";
  static const char connecting[] = "Connecting...";
  static const char disconnecting[] = "Disconnecting...";
  static const char stopped[] = "Stopped";
  static const char playing[] = "Playing";
  
  pax_col_t bgCol = pax_col_rgb(0,0,0);
  pax_background(&screenBuf, bgCol);

  pax_col_t fontColor = pax_col_rgb(255,255,255);
  const char *status = "?";
  switch (audioState.connectionState) {
    case ESP_A2D_CONNECTION_STATE_CONNECTING:
      status = connecting;
      break;
    case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
      status = disconnecting;
      break;
    case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
      status = disconnected;
      break;
    case ESP_A2D_CONNECTION_STATE_CONNECTED:
      status = (audioState.playState == ESP_A2D_AUDIO_STATE_STARTED) ? playing : stopped;
  }

  char volStr[30];
  snprintf(volStr, 30, "Volume: %i%%",audioState.volume * 100 / 127);

  pax_draw_text(&screenBuf, fontColor, pax_font_saira_condensed, pax_font_saira_condensed->default_size, 10, 10, status);
  pax_draw_text(&screenBuf, fontColor, pax_font_saira_regular, pax_font_saira_regular->default_size, 10, 90, audioState.title);
  pax_draw_text(&screenBuf, fontColor, pax_font_saira_regular, pax_font_saira_regular->default_size, 10, 115, audioState.artist);
  pax_draw_text(&screenBuf, fontColor, pax_font_saira_regular, pax_font_saira_regular->default_size, 10, 140, audioState.album);
  pax_draw_text(&screenBuf, fontColor, pax_font_saira_regular, pax_font_saira_regular->default_size, 10, 165, volStr);

  ili9341_write_partial_direct(get_ili9341(), screenBuf.buf, 0, 0, ILI9341_WIDTH, ILI9341_HEIGHT-DBMETER_HEIGHT);
  drawDBMeter();
}

void drawDBMeter() {
  if ((audioState.connectionState != ESP_A2D_CONNECTION_STATE_CONNECTED) || (audioState.playState != ESP_A2D_AUDIO_STATE_STARTED)) {
    leftDBMeter = 0;
    rightDBMeter = 0;
  }
  int halfWidth = (ILI9341_WIDTH / 2);
  float l = (leftDBMeter < 0) ? 0 : (leftDBMeter > 1) ? 1 : leftDBMeter;
  float r = (rightDBMeter < 0) ? 0 : (rightDBMeter > 1) ? 1 : rightDBMeter;
  int leftPix = halfWidth * l;
  int rightPix = halfWidth * r;
  int p1 = halfWidth - leftPix;
  int p2 = halfWidth + rightPix;
  int y = ILI9341_HEIGHT-DBMETER_HEIGHT;
  pax_col_t bgCol = pax_col_rgb(0,0,0);
  pax_col_t fgCol = pax_col_rgb(255,255,255);
  pax_simple_rect(&screenBuf, bgCol, 0,  y, p1, DBMETER_HEIGHT);
  pax_simple_rect(&screenBuf, fgCol, p1, y, p2-p1, DBMETER_HEIGHT);
  pax_simple_rect(&screenBuf, bgCol, p2, y, ILI9341_WIDTH-p2, DBMETER_HEIGHT);
  int off = 2 * ILI9341_WIDTH * (ILI9341_HEIGHT-DBMETER_HEIGHT);
  ili9341_write_partial_direct(get_ili9341(), screenBuf.buf+off, 0, ILI9341_HEIGHT-DBMETER_HEIGHT, ILI9341_WIDTH, DBMETER_HEIGHT);
}



// ----------------------

// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "bt_app_core.h"
#include "bt_app_av.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "driver/i2s.h"

/* device name */
#define LOCAL_DEVICE_NAME "BadgeBoomBox"

/* event for stack up */
enum {
    BT_APP_EVT_STACK_UP = 0,
};

/********************************
 * STATIC FUNCTION DECLARATIONS
 *******************************/

/* GAP callback function */
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
/* handler for bluetooth stack enabled events */
static void bt_av_hdl_stack_evt(uint16_t event, void *p_param);

/*******************************
 * STATIC FUNCTION DEFINITIONS
 ******************************/

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    /* when authentication completed, this event comes */
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(BT_AV_TAG, "authentication success: %s", param->auth_cmpl.device_name);
            esp_log_buffer_hex(BT_AV_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        } else {
            ESP_LOGE(BT_AV_TAG, "authentication failed, status: %d", param->auth_cmpl.stat);
        }
        break;
    }

#if (CONFIG_BT_SSP_ENABLED == true)
    /* when Security Simple Pairing user confirmation requested, this event comes */
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %d", param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    /* when Security Simple Pairing passkey notified, this event comes */
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey: %d", param->key_notif.passkey);
        break;
    /* when Security Simple Pairing passkey requested, this event comes */
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
#endif

    /* when GAP mode changed, this event comes */
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode: %d", param->mode_chg.mode);
        break;
    /* others */
    default: {
        ESP_LOGI(BT_AV_TAG, "event: %d", event);
        break;
    }
    }
}

static void bt_av_hdl_stack_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_AV_TAG, "%s event: %d", __func__, event);

    switch (event) {
    /* when do the stack up, this event comes */
    case BT_APP_EVT_STACK_UP: {
        esp_bt_dev_set_device_name(LOCAL_DEVICE_NAME);
        esp_bt_gap_register_callback(bt_app_gap_cb);

        assert(esp_avrc_ct_init() == ESP_OK);
        esp_avrc_ct_register_callback(bt_app_rc_ct_cb);
        assert(esp_avrc_tg_init() == ESP_OK);
        esp_avrc_tg_register_callback(bt_app_rc_tg_cb);

        esp_avrc_rn_evt_cap_mask_t evt_set = {0};
        esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
        assert(esp_avrc_tg_set_rn_evt_cap(&evt_set) == ESP_OK);

        assert(esp_a2d_sink_init() == ESP_OK);
        esp_a2d_register_callback(&bt_app_a2d_cb);
        esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);

        /* set discoverable and connectable mode, wait to be connected */
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        break;
    }
    /* others */
    default:
        ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
}

/*******************************
 * MAIN ENTRY POINT
 ******************************/

void bt_init(void)
{
  esp_err_t err = ESP_OK;

    /*
     * This example only uses the functions of Classical Bluetooth.
     * So release the controller memory for Bluetooth Low Energy.
     */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((err = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(err));
        return;
    }
    if ((err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(err));
        return;
    }
    if ((err = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "%s initialize bluedroid failed: %s\n", __func__, esp_err_to_name(err));
        return;
    }
    if ((err = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "%s enable bluedroid failed: %s\n", __func__, esp_err_to_name(err));
        return;
    }

    /* I2S configuration parameters */
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,              // TX only
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,       // stereo
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = 6,
        .dma_buf_len = 128,
        .intr_alloc_flags = 0,                              // default interrupt priority
        .bits_per_chan = I2S_BITS_PER_SAMPLE_16BIT,
        .tx_desc_auto_clear = true                          // auto clear tx descriptor on underflow
    };
    i2s_driver_install(0, &i2s_config, 0, NULL);

    /* enable I2S */
    i2s_pin_config_t pin_config = {
      .mck_io_num = GPIO_I2S_MCLK,
      .bck_io_num = 4, // should be GPIO_I2S_CLK
      .ws_io_num = 12, // should be GPIO_I2S_LR
      .data_out_num = GPIO_I2S_DATA,
      .data_in_num = -1                                   /* not used */
    };
    i2s_set_pin(0, &pin_config);

#if (CONFIG_BT_SSP_ENABLED == true)
    /* set default parameters for Secure Simple Pairing */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
#endif

    /* set default parameters for Legacy Pairing (use fixed pin code 1234) */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code;
    pin_code[0] = '1';
    pin_code[1] = '2';
    pin_code[2] = '3';
    pin_code[3] = '4';
    esp_bt_gap_set_pin(pin_type, 4, pin_code);

    bt_app_task_start_up();
    /* bluetooth device name, connection mode and profile set up */
    bt_app_work_dispatch(bt_av_hdl_stack_evt, BT_APP_EVT_STACK_UP, NULL, 0, NULL);
}

