/* Copyright (c) 2012 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

/* Attention! 
*  To maintain compliance with Nordic Semiconductor ASA抯 Bluetooth profile 
*  qualification listings, this section of source code must not be modified.
*/

#include "ble_spider_tunnel.h"
#include <string.h>
#include "nordic_common.h"
#include "ble_l2cap.h"
#include "ble_srv_common.h"
#include "app_util.h"
#include "boards.h"
#include "sdk_common.h"
#include "app_fifo.h"


#define OPCODE_LENGTH 1                                                    /**< Length of opcode inside Heart Rate Measurement packet. */
#define HANDLE_LENGTH 2                                                    /**< Length of handle inside Heart Rate Measurement packet. */
#define MAX_HRM_LEN   (BLE_L2CAP_MTU_DEF - OPCODE_LENGTH - HANDLE_LENGTH)  /**< Maximum size of a transmitted Heart Rate Measurement. */

#define INITIAL_VALUE_HRM                       0                          /**< Initial Heart Rate Measurement value. */

// Heart Rate Measurement flag bits
#define HRM_FLAG_MASK_HR_VALUE_16BIT           (0x01 << 0)                 /**< Heart Rate Value Format bit. */
#define HRM_FLAG_MASK_SENSOR_CONTACT_DETECTED  (0x01 << 1)                 /**< Sensor Contact Detected bit. */
#define HRM_FLAG_MASK_SENSOR_CONTACT_SUPPORTED (0x01 << 2)                 /**< Sensor Contact Supported bit. */
#define HRM_FLAG_MASK_EXPENDED_ENERGY_INCLUDED (0x01 << 3)                 /**< Energy Expended Status bit. Feature Not Supported */
#define HRM_FLAG_MASK_RR_INTERVAL_INCLUDED     (0x01 << 4)                 /**< RR-Interval bit. */


bool               ble_tx_ready = true;
static uint8_t     ble_tx_buff[256];
 app_fifo_t  ble_tx_fifo;

static __INLINE uint32_t fifo_length(app_fifo_t * p_fifo)
{
  uint32_t read_pos = p_fifo->read_pos & p_fifo->buf_size_mask;
  uint32_t write_pos = p_fifo->write_pos & p_fifo->buf_size_mask;
  if (write_pos < read_pos) 
  {
    write_pos += (p_fifo->buf_size_mask + 1);
  }
  return write_pos - read_pos;
}

/**@brief Function for handling the Connect event.
 *
 * @param[in]   p_spider_tunnel       Heart Rate Service structure.
 * @param[in]   p_ble_evt   Event received from the BLE stack.
 */
static void on_connect(ble_spider_tunnel_t * p_spider_tunnel, ble_evt_t * p_ble_evt)
{
    p_spider_tunnel->conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
}


/**@brief Function for handling the Disconnect event.
 *
 * @param[in]   p_spider_tunnel       Heart Rate Service structure.
 * @param[in]   p_ble_evt   Event received from the BLE stack.
 */
static void on_disconnect(ble_spider_tunnel_t * p_spider_tunnel, ble_evt_t * p_ble_evt)
{
    UNUSED_PARAMETER(p_ble_evt);
    p_spider_tunnel->conn_handle = BLE_CONN_HANDLE_INVALID;
}


/**@brief Function for handling write events to the Heart Rate Measurement characteristic.
 *
 * @param[in]   p_spider_tunnel         Heart Rate Service structure.
 * @param[in]   p_evt_write   Write event received from the BLE stack.
 */
static void on_hrm_cccd_write(ble_spider_tunnel_t * p_spider_tunnel, ble_gatts_evt_write_t * p_evt_write)
{
    if (p_evt_write->len == 2)
    {
        // CCCD written, update notification state
        if (p_spider_tunnel->evt_handler != NULL)
        {
            ble_hrs_evt_t evt;

            if (ble_srv_is_notification_enabled(p_evt_write->data))
            {
                evt.evt_type = BLE_HRS_EVT_NOTIFICATION_ENABLED;
            }
            else
            {
                evt.evt_type = BLE_HRS_EVT_NOTIFICATION_DISABLED;
            }

            p_spider_tunnel->evt_handler(p_spider_tunnel, &evt);
        }
    }
}


/**@brief Function for handling the Write event.
 *
 * @param[in]   p_spider_tunnel       Heart Rate Service structure.
 * @param[in]   p_ble_evt   Event received from the BLE stack.
 */
static void on_write(ble_spider_tunnel_t * p_spider_tunnel, ble_evt_t * p_ble_evt) 
{
    ble_gatts_evt_write_t * p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;

    if (p_evt_write->handle == p_spider_tunnel->tunnel_handles.value_handle)   //0x50 == 
    {

//			  LEDS_INVERT(BSP_LED_2_MASK);
			  p_spider_tunnel->tunnel_data_handler(p_spider_tunnel, p_evt_write->data, p_evt_write->len);
			
			  if (ble_srv_is_notification_enabled(p_evt_write->data))
        {
            p_spider_tunnel->is_notification_enabled = true;
        }
        else
        {
            p_spider_tunnel->is_notification_enabled = false;
        }
			
    }
	

}
//static void on_write(ble_spider_tunnel_t * p_spider_tunnel, ble_evt_t * p_ble_evt)
//{
//  ble_gatts_evt_write_t * p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;
//  if ((p_evt_write->handle == p_spider_tunnel->tunnel_handles.cccd_handle)
//      && (p_evt_write->len == 2))
//  {
//    if (ble_srv_is_notification_enabled(p_evt_write->data))
//    {
//        p_spider_tunnel->is_notification_enabled = true;
//      //  app_trace_log("Enable Notify \r\n");
////        enable_notify_callback(p_spider_tunnel);
//    }
//    else
//    {
//        p_spider_tunnel->is_notification_enabled = false;
//    }
//  }
//  else if ((p_evt_write->handle == p_spider_tunnel->tunnel_handles.value_handle)
//           && (p_spider_tunnel->tunnel_data_handler != NULL))
//  {
//    
//    p_spider_tunnel->tunnel_data_handler(p_spider_tunnel, p_evt_write->data, p_evt_write->len);
//  }
//  else if ((p_evt_write->handle == p_spider_tunnel->pass_mode_handles.value_handle)
//           && (p_spider_tunnel->pass_mode_data_handler != NULL))
//  {
//    p_spider_tunnel->pass_mode_data_handler(p_spider_tunnel, p_evt_write->data, p_evt_write->len);
//  }
//}



void ble_spider_tunnel_on_ble_evt(ble_spider_tunnel_t * p_spider_tunnel, ble_evt_t * p_ble_evt)
{
    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            on_connect(p_spider_tunnel, p_ble_evt);
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            on_disconnect(p_spider_tunnel, p_ble_evt);
            break;

        case BLE_GATTS_EVT_WRITE:
            on_write(p_spider_tunnel, p_ble_evt);
            break;
				case BLE_EVT_TX_COMPLETE:
						spider_tunnel_on_tx_complete(p_spider_tunnel);
						break; 

        default:
            // No implementation needed.
            break;
    }
}


//  
static uint32_t pass_mode_char_add(ble_spider_tunnel_t            * p_spider_tunnel,
                            const ble_spider_tunnel_init_t * p_hrs_init)
{
    /**@snippet [Adding proprietary characteristic to S110 SoftDevice] */
    ble_gatts_char_md_t char_md;
    ble_gatts_attr_md_t cccd_md;
    ble_gatts_attr_t    attr_char_value;
    ble_uuid_t          ble_uuid;
    ble_gatts_attr_md_t attr_md;
    uint8_t init_value = 0;
	
    memset(&cccd_md, 0, sizeof(cccd_md));

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);

    cccd_md.vloc = BLE_GATTS_VLOC_STACK;

    memset(&char_md, 0, sizeof(char_md));
    char_md.char_props.read   = 1;

    char_md.char_props.write_wo_resp = 1;
    char_md.p_char_user_desc  = NULL;
    char_md.p_char_pf         = NULL;
    char_md.p_user_desc_md    = NULL;
    char_md.p_cccd_md         = &cccd_md;
    char_md.p_sccd_md         = NULL;

 
	 
	BLE_UUID_BLE_ASSIGN(ble_uuid, BLE_UUID_PASS_MODE_CHARACTERISTIC);  //0xFFE2

    memset(&attr_md, 0, sizeof(attr_md));

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);

    attr_md.vloc    = BLE_GATTS_VLOC_STACK;
    attr_md.rd_auth = 0;
    attr_md.wr_auth = 0;
    attr_md.vlen    = 1;

    memset(&attr_char_value, 0, sizeof(attr_char_value));

    attr_char_value.p_uuid    = &ble_uuid;
    attr_char_value.p_attr_md = &attr_md;
    attr_char_value.init_len  = sizeof(uint8_t);
    attr_char_value.init_offs = 0;
    attr_char_value.max_len   = 20;
    attr_char_value.p_value   = &init_value;
		
    return sd_ble_gatts_characteristic_add(p_spider_tunnel->service_handle, 
                                           &char_md,
                                           &attr_char_value,
                                           &p_spider_tunnel->pass_mode_handles);
    /**@snippet [Adding proprietary characteristic to S110 SoftDevice] */
}


//
static uint32_t spider_tunnel_char_add(ble_spider_tunnel_t            * p_spider_tunnel,
                            const ble_spider_tunnel_init_t * p_hrs_init)
{
    /**@snippet [Adding proprietary characteristic to S110 SoftDevice] */
    ble_gatts_char_md_t char_md;
    ble_gatts_attr_md_t cccd_md;
    ble_gatts_attr_t    attr_char_value;
    ble_uuid_t          ble_uuid;
    ble_gatts_attr_md_t attr_md;
    uint8_t init_value = 0;
	
    memset(&char_md, 0, sizeof(char_md));
	
    char_md.char_props.notify = 1;
    char_md.char_props.read   = 1;  
    char_md.char_props.write_wo_resp = 1;
	
    char_md.p_char_user_desc  = NULL;
    char_md.p_char_pf         = NULL;
    char_md.p_user_desc_md    = NULL;
    char_md.p_cccd_md         = NULL;
    char_md.p_sccd_md         = NULL;

    BLE_UUID_BLE_ASSIGN(ble_uuid, BLE_UUID_SPIDER_TUNNEL_CHARACTERISTIC); //0xFFE1

    memset(&attr_md, 0, sizeof(attr_md));

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);

    attr_md.vloc    = BLE_GATTS_VLOC_STACK;
    attr_md.rd_auth = 0;
    attr_md.wr_auth = 0;
    attr_md.vlen    = 1;

    memset(&attr_char_value, 0, sizeof(attr_char_value));

    attr_char_value.p_uuid    = &ble_uuid;
    attr_char_value.p_attr_md = &attr_md;
    attr_char_value.init_len  = sizeof(uint8_t);
    attr_char_value.init_offs = 0;
    attr_char_value.max_len   = 20;  //define
		attr_char_value.p_value   = &init_value;
		

   return sd_ble_gatts_characteristic_add(p_spider_tunnel->service_handle,
                                           &char_md,
                                           &attr_char_value,
                                           &p_spider_tunnel->tunnel_handles);
																					 
    /**@snippet [Adding proprietary characteristic to S110 SoftDevice] */
}



uint32_t ble_spider_tunnel_init(ble_spider_tunnel_t * p_spider_tunnel, const ble_spider_tunnel_init_t * p_hrs_init)
{
    uint32_t   err_code;
    ble_uuid_t ble_uuid;
    
	  app_fifo_init(&ble_tx_fifo,ble_tx_buff,256);
	
    // Initialize service structure
	  p_spider_tunnel->evt_handler                 = p_hrs_init->evt_handler; 
	  p_spider_tunnel->tunnel_data_handler                = p_hrs_init->tunnel_data_handler;  
    p_spider_tunnel->conn_handle                 = BLE_CONN_HANDLE_INVALID;

    // Add service
    BLE_UUID_BLE_ASSIGN(ble_uuid, 0xFFE0);  //server id            //uuid 0x180D

    err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY,  //servers
                                        &ble_uuid,
                                        &p_spider_tunnel->service_handle);

    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }

    err_code = spider_tunnel_char_add(p_spider_tunnel, p_hrs_init);
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }		
		
    err_code = pass_mode_char_add(p_spider_tunnel, p_hrs_init);
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }				
		
    return NRF_SUCCESS;
}


uint32_t spider_tunnel_ble_tx(ble_spider_tunnel_t * p_nus)
{   
   	uint32_t   err_code;
    uint32_t   len;
    uint8_t    send_buff[20];	
  	uint16_t   hvx_len;
    ble_gatts_hvx_params_t hvx_params;
		
    len = fifo_length(&ble_tx_fifo);
	
    if(len < 21)
    {
      for(int i = 0; i < len;i++)
      {
        app_fifo_get(&ble_tx_fifo,&send_buff[i]);
      }
    }
    else
    {
      for(int i = 0; i < 20;i++)
      {
        app_fifo_get(&ble_tx_fifo,&send_buff[i]);
      }
      len = 20;
    }
    hvx_len =  len;
	
	
    memset(&hvx_params, 0, sizeof(hvx_params));

    hvx_params.handle = p_nus->tunnel_handles.value_handle;
    hvx_params.p_data = send_buff;
    hvx_params.p_len  = &hvx_len;
    hvx_params.type   = BLE_GATT_HVX_NOTIFICATION;
    
		hvx_params.offset = 0; //new
    err_code = sd_ble_gatts_hvx(p_nus->conn_handle, &hvx_params);
	
	  if(err_code!=NRF_SUCCESS)
    {
     // app_trace_log("sd_ble_gatts_hvx err_code: 0x%02X\r\n", err_code);
      app_fifo_flush(&ble_tx_fifo);
      ble_tx_ready = true;
    }	
    else
    {
      ble_tx_ready = false;
    }
    return err_code;
	
}


uint32_t spider_tunnel_put(ble_spider_tunnel_t * p_nus, uint8_t * p_string, uint16_t length)
{
    VERIFY_PARAM_NOT_NULL(p_nus);

    if ((p_nus->conn_handle == BLE_CONN_HANDLE_INVALID) || (p_nus->is_notification_enabled))
    {
        return NRF_ERROR_INVALID_STATE;
    }
		for(int i =0;i < length; i++)
		{
			app_fifo_put(&ble_tx_fifo,p_string[i]);
		}
	  
    if(ble_tx_ready)
    spider_tunnel_ble_tx(p_nus);
  
}







