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


/**@brief Function for handling the Connect event.
 *
 * @param[in]   p_hrs       Heart Rate Service structure.
 * @param[in]   p_ble_evt   Event received from the BLE stack.
 */
static void on_connect(ble_hrs_t * p_hrs, ble_evt_t * p_ble_evt)
{
    p_hrs->conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
}


/**@brief Function for handling the Disconnect event.
 *
 * @param[in]   p_hrs       Heart Rate Service structure.
 * @param[in]   p_ble_evt   Event received from the BLE stack.
 */
static void on_disconnect(ble_hrs_t * p_hrs, ble_evt_t * p_ble_evt)
{
    UNUSED_PARAMETER(p_ble_evt);
    p_hrs->conn_handle = BLE_CONN_HANDLE_INVALID;
}


/**@brief Function for handling write events to the Heart Rate Measurement characteristic.
 *
 * @param[in]   p_hrs         Heart Rate Service structure.
 * @param[in]   p_evt_write   Write event received from the BLE stack.
 */
static void on_hrm_cccd_write(ble_hrs_t * p_hrs, ble_gatts_evt_write_t * p_evt_write)
{
    if (p_evt_write->len == 2)
    {
        // CCCD written, update notification state
        if (p_hrs->evt_handler != NULL)
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

            p_hrs->evt_handler(p_hrs, &evt);
        }
    }
}


/**@brief Function for handling the Write event.
 *
 * @param[in]   p_hrs       Heart Rate Service structure.
 * @param[in]   p_ble_evt   Event received from the BLE stack.
 */
static void on_write(ble_hrs_t * p_hrs, ble_evt_t * p_ble_evt) //
{
    ble_gatts_evt_write_t * p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;

    if (p_evt_write->handle == p_hrs->tunnel_handles.value_handle)   //0x50 == 
    {

			  LEDS_INVERT(BSP_LED_2_MASK);
			  p_hrs->data_handler(p_hrs, p_evt_write->data, p_evt_write->len);
			
			  if (ble_srv_is_notification_enabled(p_evt_write->data))
        {
            p_hrs->is_notification_enabled = true;
        }
        else
        {
            p_hrs->is_notification_enabled = false;
        }
			
    }
	

}


void ble_hrs_on_ble_evt(ble_hrs_t * p_hrs, ble_evt_t * p_ble_evt)
{
    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            on_connect(p_hrs, p_ble_evt);
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            on_disconnect(p_hrs, p_ble_evt);
            break;

        case BLE_GATTS_EVT_WRITE:
            on_write(p_hrs, p_ble_evt);
            break;

        default:
            // No implementation needed.
            break;
    }
}


//  
static uint32_t pass_mode_char_add(ble_hrs_t            * p_hrs,
                            const ble_hrs_init_t * p_hrs_init)
{
    /**@snippet [Adding proprietary characteristic to S110 SoftDevice] */
    ble_gatts_char_md_t char_md;
    ble_gatts_attr_md_t cccd_md;
    ble_gatts_attr_t    attr_char_value;
    ble_uuid_t          ble_uuid;
    ble_gatts_attr_md_t attr_md;

    memset(&cccd_md, 0, sizeof(cccd_md));

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);

    cccd_md.vloc = BLE_GATTS_VLOC_STACK;

    memset(&char_md, 0, sizeof(char_md));
    char_md.char_props.read   = 1;
    char_md.char_props.notify = 1;
	  char_md.char_props.write         = 1;
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

    return sd_ble_gatts_characteristic_add(p_hrs->service_handle, 
                                           &char_md,
                                           &attr_char_value,
                                           &p_hrs->pass_mode_handles);
    /**@snippet [Adding proprietary characteristic to S110 SoftDevice] */
}


//
static uint32_t spider_tunnel_char_add(ble_hrs_t            * p_hrs,
                            const ble_hrs_init_t * p_hrs_init)
{
    /**@snippet [Adding proprietary characteristic to S110 SoftDevice] */
    ble_gatts_char_md_t char_md;
      ble_gatts_attr_md_t cccd_md;
    ble_gatts_attr_t    attr_char_value;
    ble_uuid_t          ble_uuid;
    ble_gatts_attr_md_t attr_md;

    memset(&char_md, 0, sizeof(char_md));
	
    char_md.char_props.notify = 1;
	  char_md.char_props.read   = 1;  
	  char_md.char_props.write         = 1;
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
    attr_char_value.max_len   = 20;

   return sd_ble_gatts_characteristic_add(p_hrs->service_handle,
                                           &char_md,
                                           &attr_char_value,
                                           &p_hrs->tunnel_handles);
																					 
    /**@snippet [Adding proprietary characteristic to S110 SoftDevice] */
}

/**@brief Function for adding the Heart Rate Measurement characteristic.
 *
 * @param[in]   p_hrs        Heart Rate Service structure.
 * @param[in]   p_hrs_init   Information needed to initialize the service.
 *
 * @return      NRF_SUCCESS on success, otherwise an error code.
 */
//static uint32_t heart_rate_measurement_char_add(ble_hrs_t            * p_hrs,
//                                                const ble_hrs_init_t * p_hrs_init)
//{
//    ble_gatts_char_md_t char_md;
//    ble_gatts_attr_md_t cccd_md;
//    ble_gatts_attr_t    attr_char_value;
//    ble_uuid_t          ble_uuid;
//    ble_gatts_attr_md_t attr_md;
//    uint8_t             encoded_initial_hrm[MAX_HRM_LEN];

//    memset(&cccd_md, 0, sizeof(cccd_md));

//    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
//    cccd_md.write_perm = p_hrs_init->hrs_hrm_attr_md.cccd_write_perm;
//    cccd_md.vloc       = BLE_GATTS_VLOC_STACK;

//    memset(&char_md, 0, sizeof(char_md));

//    char_md.char_props.notify = 1;
//	  char_md.char_props.read   = 1;   
//	//  char_md.char_props.write_wo_resp   = 1;
//	
//    char_md.p_char_user_desc  = NULL;
//    char_md.p_char_pf         = NULL;
//    char_md.p_user_desc_md    = NULL;
//    char_md.p_cccd_md         = &cccd_md;
//    char_md.p_sccd_md         = NULL;

//    BLE_UUID_BLE_ASSIGN(ble_uuid, 0xFFE9);

//    memset(&attr_md, 0, sizeof(attr_md));

//    attr_md.read_perm  = p_hrs_init->hrs_hrm_attr_md.read_perm;
//    attr_md.write_perm = p_hrs_init->hrs_hrm_attr_md.write_perm;
//    attr_md.vloc       = BLE_GATTS_VLOC_STACK;
//    attr_md.rd_auth    = 0;
//    attr_md.wr_auth    = 0;
//    attr_md.vlen       = 1;

//    memset(&attr_char_value, 0, sizeof(attr_char_value));

//    attr_char_value.p_uuid    = &ble_uuid;
//    attr_char_value.p_attr_md = &attr_md;
//    attr_char_value.init_len  = hrm_encode(p_hrs, INITIAL_VALUE_HRM, encoded_initial_hrm);
//    attr_char_value.init_offs = 0;
//    attr_char_value.max_len   = MAX_HRM_LEN;
//    attr_char_value.p_value   = encoded_initial_hrm;

//    return sd_ble_gatts_characteristic_add(p_hrs->service_handle,
//                                           &char_md,
//                                           &attr_char_value,
//                                           &p_hrs->hrm_handles);
//}




uint32_t ble_spider_tunnel_init(ble_hrs_t * p_hrs, const ble_hrs_init_t * p_hrs_init)
{
    uint32_t   err_code;
    ble_uuid_t ble_uuid;

    // Initialize service structure
	  p_hrs->evt_handler                 = p_hrs_init->evt_handler; 
	  p_hrs->data_handler                = p_hrs_init->data_handler;  
    p_hrs->conn_handle                 = BLE_CONN_HANDLE_INVALID;

    // Add service
    BLE_UUID_BLE_ASSIGN(ble_uuid, 0xFFE0);  //server id            //uuid 0x180D

    err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY,  //servers
                                        &ble_uuid,
                                        &p_hrs->service_handle);

    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }

    // Add heart rate measurement characteristic
//    err_code = heart_rate_measurement_char_add(p_hrs, p_hrs_init);
//    if (err_code != NRF_SUCCESS)
//    {
//        return err_code;
//    }

    err_code = spider_tunnel_char_add(p_hrs, p_hrs_init);
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }		
		
    err_code = pass_mode_char_add(p_hrs, p_hrs_init);
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }				
		
    return NRF_SUCCESS;
}


//uint32_t ble_hrs_heart_rate_measurement_send(ble_hrs_t * p_hrs, uint8_t * p_string, uint16_t length)
//{
//    uint32_t err_code;

//    // Send value if connected and notifying
//    if (p_hrs->conn_handle != BLE_CONN_HANDLE_INVALID)  
//    {
//        uint8_t                encoded_hrm[MAX_HRM_LEN];
//        uint16_t               len;
//        uint16_t               hvx_len;
//        ble_gatts_hvx_params_t hvx_params;

//     //   len     = hrm_encode(p_hrs, heart_rate, encoded_hrm);  
//        hvx_len = 10;

//        memset(&hvx_params, 0, sizeof(hvx_params));

//        hvx_params.handle = p_hrs->hrm_handles.value_handle;
//        hvx_params.type   = BLE_GATT_HVX_NOTIFICATION;
//        hvx_params.offset = 0;
//        hvx_params.p_len  = &hvx_len;
//        hvx_params.p_data = p_string;			
//			
//			
//        err_code = sd_ble_gatts_hvx(p_hrs->conn_handle, &hvx_params);
//        if ((err_code == NRF_SUCCESS) && (hvx_len != len))
//        {
//            err_code = NRF_ERROR_DATA_SIZE;
//        }
//    }
//    else
//    {
//        err_code = NRF_ERROR_INVALID_STATE;
//    }

//    return err_code;
//}

uint32_t ble_nus_string_send(ble_hrs_t * p_nus, uint8_t * p_string, uint16_t length)
{
    ble_gatts_hvx_params_t hvx_params;

    VERIFY_PARAM_NOT_NULL(p_nus);

    if ((p_nus->conn_handle == BLE_CONN_HANDLE_INVALID) || (p_nus->is_notification_enabled))
    {
        return NRF_ERROR_INVALID_STATE;
    }

    if (length > 20)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    memset(&hvx_params, 0, sizeof(hvx_params));

    hvx_params.handle = p_nus->tunnel_handles.value_handle;
    hvx_params.p_data = p_string;
    hvx_params.p_len  = &length;
    hvx_params.type   = BLE_GATT_HVX_NOTIFICATION;

    return sd_ble_gatts_hvx(p_nus->conn_handle, &hvx_params);
}


/*
void ble_hrs_rr_interval_add(ble_hrs_t * p_hrs, uint16_t rr_interval)
{
    if (p_hrs->rr_interval_count == BLE_HRS_MAX_BUFFERED_RR_INTERVALS)
    {
        // The rr_interval buffer is full, delete the oldest value
        memmove(&p_hrs->rr_interval[0],
                &p_hrs->rr_interval[1],
                (BLE_HRS_MAX_BUFFERED_RR_INTERVALS - 1) * sizeof(uint16_t));
        p_hrs->rr_interval_count--;
    }

    // Add new value
    p_hrs->rr_interval[p_hrs->rr_interval_count++] = rr_interval;
}


*/







/*
bool ble_hrs_rr_interval_buffer_is_full(ble_hrs_t * p_hrs)
{
    return (p_hrs->rr_interval_count == BLE_HRS_MAX_BUFFERED_RR_INTERVALS);
}


uint32_t ble_hrs_sensor_contact_supported_set(ble_hrs_t * p_hrs, bool is_sensor_contact_supported)
{
    // Check if we are connected to peer
    if (p_hrs->conn_handle == BLE_CONN_HANDLE_INVALID)
    {
        p_hrs->is_sensor_contact_supported = is_sensor_contact_supported;
        return NRF_SUCCESS;
    }
    else
    {
        return NRF_ERROR_INVALID_STATE;
    }
}


void ble_hrs_sensor_contact_detected_update(ble_hrs_t * p_hrs, bool is_sensor_contact_detected)
{
    p_hrs->is_sensor_contact_detected = is_sensor_contact_detected;
}
*/
/*
uint32_t ble_hrs_body_sensor_location_set(ble_hrs_t * p_hrs, uint8_t body_sensor_location)
{
    ble_gatts_value_t gatts_value;

    // Initialize value struct.
    memset(&gatts_value, 0, sizeof(gatts_value));

    gatts_value.len     = sizeof(uint8_t);
    gatts_value.offset  = 0;
    gatts_value.p_value = &body_sensor_location;

    return sd_ble_gatts_value_set(p_hrs->conn_handle, p_hrs->bsl_handles.value_handle, &gatts_value);
}
*/