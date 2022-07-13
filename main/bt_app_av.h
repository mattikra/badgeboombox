/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifndef __BT_APP_AV_H__
#define __BT_APP_AV_H__

#include <stdint.h>
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

/** the full exposed audio state in a struct */
#define AUDIOSTATE_STRLEN 100
typedef struct BTAudioState_ {
  esp_a2d_connection_state_t connectionState;  // 0=disconnected, 1=connecting, 2=connected, 3=disconnecting
  esp_a2d_audio_state_t playState;  //0=suspended, 1=stopped, 2=playing
  uint8_t volume; //0..127
  int sampleRate;
  char title[AUDIOSTATE_STRLEN];
  char artist[AUDIOSTATE_STRLEN];
  char album[AUDIOSTATE_STRLEN];
} BTAudioState;

/** returns the current audio state in a thread-safe way
 * @param outState pointer to receive the audio state */
void getAudioState(BTAudioState *outState);

/** callback when audio state changes */
typedef void (*AudioStateChangeCB)();

/** set the callback to be called when the audio state changes 
 * @param cb callback to set */
void setAudioStateChangeCB(AudioStateChangeCB cb);

/** returns the current volume
 * @return current volume (0..127) */
uint8_t getVolume();


/* log tags */
#define BT_AV_TAG       "BT_AV"
#define BT_RC_TG_TAG    "RC_TG"
#define BT_RC_CT_TAG    "RC_CT"

/**
 * @brief  callback function for A2DP sink
 *
 * @param [in] event  event id
 * @param [in] param  callback parameter
 */
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

/**
 * @brief  callback function for A2DP sink audio data stream
 *
 * @param [out] data  data stream writteen by application task
 * @param [in]  len   length of data stream in byte
 */
void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len);

/**
 * @brief  callback function for AVRCP controller
 *
 * @param [in] event  event id
 * @param [in] param  callback parameter
 */
void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

/**
 * @brief  callback function for AVRCP target
 *
 * @param [in] event  event id
 * @param [in] param  callback parameter
 */
void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param);

/**
 * @brief set volume
 * 
 * @param[in] volume volume to set (0..127)
 */
void volume_set_by_local_host(uint8_t volume);

#endif /* __BT_APP_AV_H__*/
