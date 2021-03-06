

#include <stdint.h>
#include <string.h>
#include "nordic_common.h"
#include "nrf.h"
#include "app_error.h"
#include "ble.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_dis.h"
#ifdef BLE_DFU_APP_SUPPORT
#include "ble_dfu.h"
#include "dfu_app_handler.h"
#endif // BLE_DFU_APP_SUPPORT

#include "app_util_platform.h"
#include "ble_conn_params.h"
#include "boards.h"
#include "softdevice_handler.h"
#include "app_timer.h"
#include "app_trace.h"
#include "device_manager.h"
#include "pstorage.h"
#include "bsp.h"
#include "bsp_btn_ble.h"
#include "version.h"
#include "nrf_drv_spi.h"
#include "nrf_drv_timer.h"
#include "nrf_delay.h"
//#include "custom_board.h"
#include "radio.h"
#include "sx1276.h"
#include "sx1276-hal.h"
#include "sx1276-LoRa.h"
#include "sx1276-LoRaMisc.h"
#include "config.h"
#include "app_scheduler.h"
#include "app_timer_appsh.h"
#include "datatype.h"
#include "PacketEncode.h"
#include "app_fifo.h"
#include "ble_spider_tunnel.h"
#include "bmp280.h"
#include "twi_master.h"



//#define LEDS_CONFIGURE(leds_mask) do { uint32_t pin;                  \
//                                  for (pin = 0; pin < 32; pin++) \
//                                      if ( (leds_mask) & (1 << pin) )   \
//                                          nrf_gpio_cfg_output(pin); } while (0)
																
const   nrf_drv_spi_t spi = NRF_DRV_SPI_INSTANCE(SPI_INSTANCE);  /**< SPI instance. */
static  volatile bool spi_xfer_done;  /**< Flag used to indicate that SPI instance completed the transfer. */

//Init Timer tick
__IO uint32_t     uwTick;
nrf_drv_timer_t   TIMER_ONEMS = NRF_DRV_TIMER_INSTANCE(1);  //Selcet Timer1
APP_TIMER_DEF(m_heart_rate_timer_id);                                              
uint8_t           one_ms_task_flag  = 0;
uint8_t           one_sec_task_flag = 0;
uint32_t          thund_ms          = 0;
uint8_t           get_bmp280_ms     = 0;
uint8_t           get_bmp280_flag   = 0;
	
uint8_t   id = 0;
uint8_t   radioReady                  = 0;
uint8_t   channel                     = 1;      //set channel of freq
uint32_t  deviceCount                 = 6;
static    uint32_t channel_local      = 1;
static    uint32_t total_number_local = 6;
static    uint32_t pa_gain_voltage    = 0;
//static    float press_local = 1000.0f;
//static    float temp_local  = 25.0f;


static    pstorage_block_t          pstorage_wait_handle 			= 0;
static    uint8_t                   pstorage_wait_flag   			= 0;
static    uint32_t                  voltage_first_read_flag 	= 0;
static    pstorage_handle_t	        block_ptsorage_handle;
static    pstorage_handle_t         pstorage_handle;
static    pstorage_module_param_t   pstorage_param;

LocationStatus locationStatus;
GpsPressure    gpsPressure;
Dispatch       dispatch;
Request        request;
Response       response;

uint32_t  timeSlot 								= 33;
uint32_t  preDataTimestamp[16] 		= {0};
float     pressure 								= 1020.0f;
float     temperature 						= 25.0f;
bool      pressure_valid          = false;
bool      reference_valid         = false;
bool      rfTxBusyFlag            = false;
bool      changeChannelFlag       = false;
bool      channel_confirmed       = false;
bool      total_number_confirmed  = false;

uint8_t   dispatchCmdCount = 0;
uint32_t  RadioChannelFrequencies[] = {
  868000000,
  870000000,
  872000000,
  874000000,
  876000000,
  878000000,
  880000000,
  882000000,
  884000000,
  886000000,
};

uint8_t   repeaterTTLMax[7] 			= {0}; // Mark the Max repeater TTL
uint8_t   repeaterValidTimeout[7] = {0}; // Mark Valid Time Tick;
uint8_t   passValidTimeout[12] 		= {0};
uint8_t   referenceValidTimeout 	= 0;
//extern    static double RxPacketRssiValue;


static dm_application_instance_t    m_app_handle;                              /**< Application identifier allocated by device manager */
static ble_spider_tunnel_t          m_spider_tunnel;    
static uint16_t                     m_conn_handle = BLE_CONN_HANDLE_INVALID; 

static ble_uuid_t m_adv_uuids[] = {{BLE_UUID_HEART_RATE_SERVICE,         BLE_UUID_TYPE_BLE},        
                                   {BLE_UUID_DEVICE_INFORMATION_SERVICE, BLE_UUID_TYPE_BLE}}; /**< Universally unique service identifiers. */

																	 
static uint8_t FICR_CONFIG_ID[8] 					= {0};
static uint8_t HWID_Str[65] 							= {0};
static uint8_t FWID_Str[8] 								= {0};
static uint8_t device_name_with_addr[32] 	= {0};
static uint8_t BLE_ADDR[32] 							= {0};
static uint8_t SW_VERSION[64] 						= {0};																	 
											
static    ble_spider_tunnel_t   m_spider_tunnel;
static    uint8_t tra_data[20];																	 
uint32_t  time_ticks;
static    app_fifo_t ble_rx_fifo;
static    uint8_t ble_rx_buff [256];
uint8_t   ble_rx_buff_group[5][128] = {0};
uint8_t   ble_rx_index 							= 0;


typedef struct
{
  uint16_t len;
  uint8_t* buff;
} transmit_rx_data;
transmit_rx_data   ble_rx_array[5] = {0};


#ifdef BLE_DFU_APP_SUPPORT
static ble_dfu_t  m_dfus;                                    /**< Structure used to identify the DFU service. */
#endif // BLE_DFU_APP_SUPPORT

 
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


/**@brief Callback function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num   Line number of the failing ASSERT call.
 * @param[in] file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

/**@brief Function for the Power manager.
 */
static void power_manage(void)
{
    uint32_t err_code = sd_app_evt_wait();
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling the Heart rate measurement timer timeout.
 *
 * @details This function will be called each time the heart rate measurement timer expires.
 *          It will exclude RR Interval data from every third measurement.
 *
 * @param[in] p_context  Pointer used for passing some arbitrary information (context) from the
 *                       app_start_timer() call to the timeout handler.
 */
static void heart_rate_meas_timeout_handler(void * p_context)
{
}


void one_ms_timeout_handler(nrf_timer_event_t event_type, void* p_context)
{
	UNUSED_PARAMETER(p_context);
  switch(event_type)
	{
			case NRF_TIMER_EVENT_COMPARE0:
					uwTick++;
			    thund_ms++;
			    get_bmp280_ms++;
			   
			    if(thund_ms == 2999)
					{
						one_sec_task_flag = 1;
						thund_ms = 0;
					}
					if(get_bmp280_ms > 38)
					{
					  get_bmp280_flag = 1;
					}
					
			    one_ms_task_flag = 1;
			
					break;
			default:
					//Do nothing.
					break;
	}   
}

/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module. This creates and starts application timers.
 */
static void timers_init(void)
{
    uint32_t err_code;
    
    // Initialize timer module.
    APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, false);   

    err_code = app_timer_create(&m_heart_rate_timer_id, APP_TIMER_MODE_REPEATED,
                                heart_rate_meas_timeout_handler);
    APP_ERROR_CHECK(err_code);
	   
	  //Init Timer 1 for 1ms tick
    err_code = nrf_drv_timer_init(&TIMER_ONEMS, NULL, one_ms_timeout_handler);
    APP_ERROR_CHECK(err_code);
  	time_ticks = nrf_drv_timer_ms_to_ticks(&TIMER_ONEMS,1);
  	nrf_drv_timer_extended_compare(&TIMER_ONEMS, NRF_TIMER_CC_CHANNEL0, time_ticks, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);
    nrf_drv_timer_enable(&TIMER_ONEMS);
	
}


/**@brief Function for the GAP initialization.
 *
 * @details This function sets up all the necessary GAP (Generic Access Profile) parameters of the
 *          device including the device name, appearance, and the preferred connection parameters.
 */
static void gap_params_init(void)
{
    uint32_t                err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(&sec_mode,
                                          (const uint8_t *)DEVICE_NAME,
                                          strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);

    err_code = sd_ble_gap_appearance_set(BLE_APPEARANCE_HEART_RATE_SENSOR_HEART_RATE_BELT);
    APP_ERROR_CHECK(err_code);

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}


#ifdef BLE_DFU_APP_SUPPORT
/**@brief Function for stopping advertising.
 */
static void advertising_stop(void)
{
    uint32_t err_code;

    err_code = sd_ble_gap_adv_stop();
    APP_ERROR_CHECK(err_code);

    err_code = bsp_indication_set(BSP_INDICATE_IDLE);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for loading application-specific context after establishing a secure connection.
 *
 * @details This function will load the application context and check if the ATT table is marked as
 *          changed. If the ATT table is marked as changed, a Service Changed Indication
 *          is sent to the peer if the Service Changed CCCD is set to indicate.
 *
 * @param[in] p_handle The Device Manager handle that identifies the connection for which the context
 *                     should be loaded.
 */
static void app_context_load(dm_handle_t const * p_handle)
{
    uint32_t                 err_code;
    static uint32_t          context_data;
    dm_application_context_t context;

    context.len    = sizeof(context_data);
    context.p_data = (uint8_t *)&context_data;

    err_code = dm_application_context_get(p_handle, &context);
    if (err_code == NRF_SUCCESS)
    {
        // Send Service Changed Indication if ATT table has changed.
        if ((context_data & (DFU_APP_ATT_TABLE_CHANGED << DFU_APP_ATT_TABLE_POS)) != 0)
        {
            err_code = sd_ble_gatts_service_changed(m_conn_handle, APP_SERVICE_HANDLE_START, BLE_HANDLE_MAX);
            if ((err_code != NRF_SUCCESS) &&
                (err_code != BLE_ERROR_INVALID_CONN_HANDLE) &&
                (err_code != NRF_ERROR_INVALID_STATE) &&
                (err_code != BLE_ERROR_NO_TX_PACKETS) &&
                (err_code != NRF_ERROR_BUSY) &&
                (err_code != BLE_ERROR_GATTS_SYS_ATTR_MISSING))
            {
                APP_ERROR_HANDLER(err_code);
            }
        }

        err_code = dm_application_context_delete(p_handle);
        APP_ERROR_CHECK(err_code);
    }
    else if (err_code == DM_NO_APP_CONTEXT)
    {
        // No context available. Ignore.
    }
    else
    {
        APP_ERROR_HANDLER(err_code);
    }
}


/** @snippet [DFU BLE Reset prepare] */
/**@brief Function for preparing for system reset.
 *
 * @details This function implements @ref dfu_app_reset_prepare_t. It will be called by
 *          @ref dfu_app_handler.c before entering the bootloader/DFU.
 *          This allows the current running application to shut down gracefully.
 */
static void reset_prepare(void)
{
    uint32_t err_code;

    if (m_conn_handle != BLE_CONN_HANDLE_INVALID)
    {
        // Disconnect from peer.
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        APP_ERROR_CHECK(err_code);
        err_code = bsp_indication_set(BSP_INDICATE_IDLE);
        APP_ERROR_CHECK(err_code);
    }
    else
    {
        // If not connected, the device will be advertising. Hence stop the advertising.
        advertising_stop();
    }

    err_code = ble_conn_params_stop();
    APP_ERROR_CHECK(err_code);

    nrf_delay_ms(500);
}
/** @snippet [DFU BLE Reset prepare] */
#endif // BLE_DFU_APP_SUPPORT


/****************  FLASH  START  ****************/
static void flash_store_channel(void)
{
    pstorage_wait_handle = block_ptsorage_handle.block_id;            //Specify which pstorage handle to wait for
		pstorage_wait_flag   = 1;      
	  pstorage_update(&block_ptsorage_handle, (uint8_t *)&channel_local, 4, 0);     //Write to flash, only one block is allowed for each pstorage_store command
    while(pstorage_wait_flag) { power_manage(); }   		//Clear 16 bytes
}

static void flash_store_total_number()
{

    pstorage_wait_handle = block_ptsorage_handle.block_id;            //Specify which pstorage handle to wait for
		pstorage_wait_flag = 1;      
	  pstorage_update(&block_ptsorage_handle, (uint8_t *)&total_number_local, 4, 4);     //Write to flash, only one block is allowed for each pstorage_store command
    while(pstorage_wait_flag) { power_manage(); }   		//Clear 16 bytes
}
static void flash_store_voltage()
{

    pstorage_wait_handle = block_ptsorage_handle.block_id;            //Specify which pstorage handle to wait for
		pstorage_wait_flag = 1;      
	  pstorage_update(&block_ptsorage_handle, (uint8_t *)&pa_gain_voltage, 4, 8);     //Write to flash, only one block is allowed for each pstorage_store command
    while(pstorage_wait_flag) { power_manage(); }   		//Clear 16 bytes
}
static void flash_store_voltage_first_read_flag()
{

    pstorage_wait_handle = block_ptsorage_handle.block_id;            //Specify which pstorage handle to wait for
		pstorage_wait_flag = 1;      
	  pstorage_update(&block_ptsorage_handle, (uint8_t *)&voltage_first_read_flag, 4, 12);     //Write to flash, only one block is allowed for each pstorage_store command
    while(pstorage_wait_flag) { power_manage(); }   		//Clear 16 bytes
}
static void example_cb_handler(pstorage_handle_t  * handle,
															 uint8_t              op_code,
                               uint32_t             result,
                               uint8_t            * p_data,
                               uint32_t             data_len)
{
		if(handle->block_id == pstorage_wait_handle) { pstorage_wait_flag = 0; }  //If we are waiting for this callback, clear the wait flag.
			
}

static void flash_init()
{	
    uint32_t                   err_code;
		pstorage_param.block_size  = 16;                    //Select block size of 8 bytes
		pstorage_param.block_count = 1;                     //Select 1 blocks, total of 8 bytes
		pstorage_param.cb          = example_cb_handler;    //Set the pstorage callback handler
		err_code = pstorage_register(&pstorage_param, &pstorage_handle);
    APP_ERROR_CHECK(err_code);
	
	//Get block identifiers
		err_code = pstorage_block_identifier_get(&pstorage_handle, 0, &block_ptsorage_handle);
    APP_ERROR_CHECK(err_code);
   
    pstorage_wait_handle = block_ptsorage_handle.block_id;            //Specify which pstorage handle to wait for
		pstorage_wait_flag = 1;      
	  pstorage_load((uint8_t *)&channel_local,&block_ptsorage_handle,  4, 0);     //Write to flash, only one block is allowed for each pstorage_store command
    while(pstorage_wait_flag) { power_manage(); }   	

		pstorage_wait_handle = block_ptsorage_handle.block_id;            //Specify which pstorage handle to wait for
		pstorage_wait_flag = 1;      
	  pstorage_load((uint8_t *)&total_number_local,&block_ptsorage_handle,  4, 4);     //Write to flash, only one block is allowed for each pstorage_store command
    while(pstorage_wait_flag) { power_manage(); }   	
    
    pstorage_wait_handle = block_ptsorage_handle.block_id;            //Specify which pstorage handle to wait for
		pstorage_wait_flag = 1;      
	  pstorage_load((uint8_t *)&pa_gain_voltage,&block_ptsorage_handle,  4, 8);     //Write to flash, only one block is allowed for each pstorage_store command
    while(pstorage_wait_flag) { power_manage(); }   		//Clear 16 bytes
    
    pstorage_wait_handle = block_ptsorage_handle.block_id;            //Specify which pstorage handle to wait for
		pstorage_wait_flag = 1;      
	  pstorage_load((uint8_t *)&voltage_first_read_flag,&block_ptsorage_handle,  4, 12);     //Write to flash, only one block is allowed for each pstorage_store command
    while(pstorage_wait_flag) { power_manage(); }   		//Clear 16 bytes
    
    if(channel_local > 10)
    {
      channel_local = 1;
      app_sched_event_put(NULL, 0, flash_store_channel);
    }
    if(total_number_local > 12 )
    {
      total_number_local = 6;
      app_sched_event_put(NULL, 0, flash_store_total_number);
    }
    if(pa_gain_voltage > 3300 || pa_gain_voltage < 1 || voltage_first_read_flag != 123456)
    {
      pa_gain_voltage = 2800;
      app_sched_event_put(NULL, 0, flash_store_voltage);
      voltage_first_read_flag = 123456;
      app_sched_event_put(NULL, 0, flash_store_voltage_first_read_flag);
    }
}

/*******************  FLASH END  ***********************/



void ble_packet_handler(void * p_event_data, uint16_t event_size)
{
  uint8_t  decode_buff[128],encode_buff[128];
  uint8_t  packet_type;
  uint16_t crc_local;
  uint32_t decode_len,encode_len,crc_len;
  transmit_rx_data* ble_recv_once = (transmit_rx_data*)p_event_data;\
	
	uint32_t  parameter = 0.0f;
	uint8_t   responseFlag = 0;
	Request   requestRx;

  decode_len = DecodePacket(ble_recv_once->buff, decode_buff, ble_recv_once->len);
 // __nop();
  packet_type = decode_buff[0];
	
	switch(packet_type)
  {
    case RequestType : 
    {
      if(decode_len == sizeof(Request))   //CRC校验
      {
        Request request;
        memcpy(&request,decode_buff,decode_len);   
        crc_local = CRC16(decode_buff,0,sizeof(Request)-2);
        if(crc_local == request.CRC16)
        {
					if(request.Cmd == CMD_RESET_ALGORITHM)
					{
							dispatchCmdCount = 3;

							dispatch.Cmd = (1 << CMD_BIT_RESET_ALGORITHM);
							dispatch.Head = requestRx.Parameter &0x0F; // Copy ID from parameter
							dispatch.Head |= 0x7000;  // Mark TTL to 7;
							response.Parameter = requestRx.Parameter;
							responseFlag = 1;
							break;
					}
					else if(request.Cmd == CMD_STOP_TRACKING)
					{
							dispatchCmdCount = 3;
							dispatch.Cmd = (1 << CMD_BIT_STOP);
							dispatch.Head = requestRx.Parameter &0x0F; // Copy ID from parameter
							dispatch.Head |= 0x7000;  // Mark TTL to 7;
							response.Parameter = requestRx.Parameter;
							responseFlag = 1;
							break;
					}						
					else if(request.Cmd == CMD_RETREAT)
					{               
							dispatchCmdCount = 3;
							dispatch.Cmd = (1 << CMD_BIT_RETREAT);
							dispatch.Head = requestRx.Parameter & 0x0F; // Copy ID from parameter
							dispatch.Head |= 0x7000;  // Mark TTL to 7;
							response.Parameter = requestRx.Parameter;
							responseFlag = 1;
							break;
					}			
					else if(request.Cmd == CMD_UPDATE_PRESSURE)
					{
							// Align the data to 4 byte address
							parameter = requestRx.Parameter;
							memcpy(&pressure, &parameter, 4);
							response.Parameter = requestRx.Parameter;
							responseFlag = 0;
							// Update reference timeout for dispatch status
							gpsPressure.Head &= (~0x08);
							gpsPressure.Head |= 0x08;
							referenceValidTimeout = 0;
							break;						
					}
				  else if(request.Cmd == CMD_UPDATE_TEMPERATURE)
					{
							// Align the data to 4 byte address
							parameter = requestRx.Parameter;
							memcpy(&temperature, &parameter, 4);
							response.Parameter = requestRx.Parameter;
							responseFlag = 0;
							gpsPressure.Head &= (~0x04);
							gpsPressure.Head |= 0x04;
							break;
					}
				  else if(request.Cmd == CMD_UPDATE_TEMPERATURE)
					{
							if (requestRx.Parameter <= 10)
							{
								dispatch.Battery &= (~0x0F);
								dispatch.Battery |=  (requestRx.Parameter & 0x0F);
							}
							responseFlag = 0;
							break;
					}
//				  if(request.Cmd == CMD_GET_REVISION_NUMBER)
//					{
//						
//					}


					
					
          else if(request.Cmd == CMD_CHANNEL)
          {
            if(channel_local != request.Parameter)
            {
              channel_local = request.Parameter;
							changeChannelFlag = true;  //new
              app_sched_event_put(NULL, 0, flash_store_channel);  //update local data
            }
          }
					
          else if(request.Cmd == CMD_TOTAL_NUMBER)
          {
            if(total_number_local != request.Parameter)
            {
              total_number_local = request.Parameter;
							dispatch.TotalNumber = requestRx.Parameter;   //new
							
              app_sched_event_put(NULL, 0, flash_store_total_number);
            }
          }
          else if(request.Cmd == CMD_PA_GAIN_VOLTAGE)
          {
            if(request.Parameter > 0 && request.Parameter <= 3300 )
            {
              pa_gain_voltage = request.Parameter;
              app_sched_event_put(NULL, 0, flash_store_voltage);
            }
          }
					
          //app_uart_putsend(ble_recv_once->buff,ble_recv_once->len);
				//	SX1276SetTxPacket(ble_recv_once->buff,ble_recv_once->len);
	      //  SX1276LoRaProcess();					
					
					
        }
      }        
      break;
    }
    case ResponseType : 
    {
      if(decode_len == sizeof(Response))   //CRC校验
      {
        Response response;
        memcpy(&response,decode_buff,decode_len);
        crc_local = CRC16(decode_buff,0,sizeof(Response)-2);
        if(crc_local == response.CRC16)
        {
          if(response.Cmd == CMD_CHANNEL)
          {
            if(channel_local != response.Parameter)
            {
              channel_local = response.Parameter;
              app_sched_event_put(NULL, 0, flash_store_channel);//
            }
            channel_confirmed = true;
          }
          else if(response.Cmd == CMD_TOTAL_NUMBER)
          {
            if(total_number_local != response.Parameter)
            {
              total_number_local = response.Parameter;
              app_sched_event_put(NULL, 0, flash_store_total_number);//
            }
            total_number_confirmed = true;
          }
          //app_uart_putsend(ble_recv_once->buff,ble_recv_once->len);
				//	SX1276SetTxPacket(ble_recv_once->buff,ble_recv_once->len);
	      //  SX1276LoRaProcess();
					
        }
      }        
      break;
    }
    default :
    //  app_uart_putsend(ble_recv_once->buff,ble_recv_once->len);//
		  SX1276SetTxPacket(ble_recv_once->buff,ble_recv_once->len);
	    SX1276LoRaProcess();
      break;  
  }
}




void spider_tunnel_data_handler(ble_spider_tunnel_t * p_spider_tunnel, uint8_t * p_data, uint16_t length)
{
  uint8_t*  head_pointer;
  uint8_t   ch;
  uint32_t  fifo_len;
  uint8_t   buff[256] = {0};
  uint32_t  err_code;
	
  for(uint32_t i = 0; i < length; i++)  
  {
    ch = p_data[i];
    if (ch == 0xB6)
    {
      app_fifo_flush(&ble_rx_fifo);
    }
    
    app_fifo_put(&ble_rx_fifo, ch);
    
    if (ch == 0xDB)
    {
      ble_rx_array[ble_rx_index].len = fifo_length(&ble_rx_fifo);
      for(uint32_t i = 0; i < ble_rx_array[ble_rx_index].len;i++)
      {
        app_fifo_get(&ble_rx_fifo, &ble_rx_buff_group[ble_rx_index][i]);
      }
      ble_rx_array[ble_rx_index].buff = ble_rx_buff_group[ble_rx_index];
      err_code = app_sched_event_put(&(ble_rx_array[ble_rx_index]), sizeof(transmit_rx_data), ble_packet_handler);
			
			APP_ERROR_CHECK(err_code);
			
      ble_rx_index = (ble_rx_index + 1) % 5;
			
      app_fifo_flush(&ble_rx_fifo);
    }
  }
}

static void nus_data_handler(ble_spider_tunnel_t * p_nus, uint8_t * p_data, uint16_t length)
{
	  uint8_t dis_data[120] = {0};
    for (uint32_t i = 0; i < length; i++)
    {
         dis_data[i] = p_data[i];
    }  		
		spider_tunnel_data_handler(p_nus,dis_data,length);
}


/**@brief Function for initializing services that will be used by the application.
 * 
 * @details Initialize the Heart Rate, Battery and Device Information services.
 */
static void services_init(void)
{
    uint32_t                   err_code;
	  uint8_t                    addr[8] = {0};
  	ble_dis_init_t             dis_init;	           //系统信息服务
	  ble_spider_tunnel_init_t   spider_tunnel_init;   //心跳服务
	  
	 
    memset(&spider_tunnel_init, 0, sizeof(spider_tunnel_init));
		
    spider_tunnel_init.evt_handler  = NULL;
    spider_tunnel_init.tunnel_data_handler = nus_data_handler;
		

    // Here the sec level for the Heart Rate Service can be changed/increased.
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&spider_tunnel_init.hrs_hrm_attr_md.cccd_write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&spider_tunnel_init.hrs_hrm_attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&spider_tunnel_init.hrs_hrm_attr_md.write_perm);

    err_code = ble_spider_tunnel_init(&m_spider_tunnel, &spider_tunnel_init);
    APP_ERROR_CHECK(err_code);

    // Initialize Device Information Service.
    memset(&dis_init, 0, sizeof(dis_init));
    //设置dis
    memcpy(FICR_CONFIG_ID, NRF_FICR->DEVICEID, 8);
    memcpy(addr, NRF_FICR->DEVICEADDR, 8);
		
    sprintf(HWID_Str, "%04X%04X %uK %uK %u", FICR_CONFIG_ID[1], FICR_CONFIG_ID[0],NRF_FICR->INFO.RAM,NRF_FICR->INFO.FLASH,NRF_FICR->INFO.VARIANT);
    sprintf(FWID_Str, "%02X%02X", FICR_CONFIG_ID[3], FICR_CONFIG_ID[2]);
    sprintf(BLE_ADDR, "%02X:%02X:%02X:%02X:%02X:%02X", addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
	  sprintf(SW_VERSION, "%s", GIT_VERSION);
	
    ble_srv_ascii_to_utf8(&dis_init.manufact_name_str, (char *)MANUFACTURER_NAME);
		ble_srv_ascii_to_utf8(&dis_init.model_num_str, (char *)MODEL_NUM);
		ble_srv_ascii_to_utf8(&dis_init.serial_num_str, (char *)BLE_ADDR);
		
		ble_srv_ascii_to_utf8(&dis_init.hw_rev_str, (char *)HWID_Str);
    ble_srv_ascii_to_utf8(&dis_init.fw_rev_str, (char *)FWID_Str);
    ble_srv_ascii_to_utf8(&dis_init.sw_rev_str, (char *)SW_VERSION);
		
		
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&dis_init.dis_attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&dis_init.dis_attr_md.write_perm);
 
    dis_init.sw_rev_str.p_str = GIT_VERSION;	
    err_code = ble_dis_init(&dis_init);
    APP_ERROR_CHECK(err_code);


#ifdef BLE_DFU_APP_SUPPORT
    /** @snippet [DFU BLE Service initialization] */
    ble_dfu_init_t   dfus_init;

    // Initialize the Device Firmware Update Service.
    memset(&dfus_init, 0, sizeof(dfus_init));

    dfus_init.evt_handler   = dfu_app_on_dfu_evt;
    dfus_init.error_handler = NULL;
    dfus_init.evt_handler   = dfu_app_on_dfu_evt;
    dfus_init.revision      = DFU_REVISION;

    err_code = ble_dfu_init(&m_dfus, &dfus_init);
    APP_ERROR_CHECK(err_code);

    dfu_app_reset_prepare_set(reset_prepare);
    dfu_app_dm_appl_instance_set(m_app_handle);
    /** @snippet [DFU BLE Service initialization] */
#endif // BLE_DFU_APP_SUPPORT
}



/**@brief Function for starting application timers.
 */
static void application_timers_start(void)
{
    uint32_t err_code;

    err_code = app_timer_start(m_heart_rate_timer_id, HEART_RATE_MEAS_INTERVAL, NULL);  
    APP_ERROR_CHECK(err_code);

}


/**@brief Function for handling the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module which
 *          are passed to the application.
 *          @note All this function does is to disconnect. This could have been done by simply
 *                setting the disconnect_on_fail config parameter, but instead we use the event
 *                handler mechanism to demonstrate its use.
 *
 * @param[in] p_evt  Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
    uint32_t err_code;

    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
    {
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_CHECK(err_code);
    }
}


/**@brief Function for handling a Connection Parameters error.
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void)
{
    uint32_t               err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
  //  cp_init.start_on_notify_cccd_handle    = m_spider_tunnel.hrm_handles.cccd_handle;
	  cp_init.start_on_notify_cccd_handle    = m_spider_tunnel.tunnel_handles.cccd_handle;
	
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = on_conn_params_evt;
    cp_init.error_handler                  = conn_params_error_handler;

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for putting the chip into sleep mode.
 *
 * @note This function will not return.
 */
static void sleep_mode_enter(void)
{
    uint32_t err_code = bsp_indication_set(BSP_INDICATE_IDLE);
    APP_ERROR_CHECK(err_code);

    // Prepare wakeup buttons.
    err_code = bsp_btn_ble_sleep_mode_prepare();
    APP_ERROR_CHECK(err_code);

    // Go to system-off mode (this function will not return; wakeup will cause a reset).
    err_code = sd_power_system_off();
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
    uint32_t err_code;

    switch (ble_adv_evt)
    {
        case BLE_ADV_EVT_FAST:
            err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING);
            APP_ERROR_CHECK(err_code);
            break;
        case BLE_ADV_EVT_IDLE:
            sleep_mode_enter();
            break;
        default:
            break;
    }
}


void spider_tunnel_on_tx_complete(ble_spider_tunnel_t * p_spider_tunnel)
{
  uint32_t len;
  uint8_t send_buff[20];
	extern  app_fifo_t  ble_tx_fifo;
	extern  bool        ble_tx_ready;
	
  len = fifo_length(&ble_tx_fifo);
  if(len == 0)
  {
    ble_tx_ready = true;
  }
  else
  {
    spider_tunnel_ble_tx(p_spider_tunnel);
  }


}
/**@brief Function for handling the Application's BLE Stack events.
 *
 * @param[in] p_ble_evt  Bluetooth stack event.
 */
void ble_on_ble_evt( ble_evt_t * p_ble_evt)
{
    uint32_t err_code;

    switch (p_ble_evt->header.evt_id)
            {
        case BLE_GAP_EVT_CONNECTED:
            err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);
            APP_ERROR_CHECK(err_code);
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
            break;


        default:
            // No implementation needed.
            break;
    }
}


/**@brief Function for dispatching a BLE stack event to all modules with a BLE stack event handler.
 *
 * @details This function is called from the BLE Stack event interrupt handler after a BLE stack
 *          event has been received.
 *
 * @param[in] p_ble_evt  Bluetooth stack event.
 */
static void ble_evt_dispatch(ble_evt_t * p_ble_evt)
{
    dm_ble_evt_handler(p_ble_evt);
    ble_conn_params_on_ble_evt(p_ble_evt);	
    ble_advertising_on_ble_evt(p_ble_evt);

	

//    bsp_btn_ble_on_ble_evt(p_ble_evt);      //button event

	
#ifdef BLE_DFU_APP_SUPPORT
    /** @snippet [Propagating BLE Stack events to DFU Service] */
    ble_dfu_on_ble_evt(&m_dfus, p_ble_evt);
    /** @snippet [Propagating BLE Stack events to DFU Service] */
#endif // BLE_DFU_APP_SUPPORT
  
    ble_on_ble_evt(p_ble_evt);	
    ble_spider_tunnel_on_ble_evt(&m_spider_tunnel, p_ble_evt);  //ble event
	
}


/**@brief Function for dispatching a system event to interested modules.
 *
 * @details This function is called from the System event interrupt handler after a system
 *          event has been received.
 *
 * @param[in] sys_evt  System stack event.
 */
static void sys_evt_dispatch(uint32_t sys_evt)
{
    pstorage_sys_event_handler(sys_evt);
    ble_advertising_on_sys_evt(sys_evt);
}


/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
    uint32_t err_code;
    
    nrf_clock_lf_cfg_t clock_lf_cfg = NRF_CLOCK_LFCLKSRC;
    
    // Initialize the SoftDevice handler module.	
    SOFTDEVICE_HANDLER_INIT(&clock_lf_cfg, NULL);

    ble_enable_params_t ble_enable_params;
    err_code = softdevice_enable_get_default_config(CENTRAL_LINK_COUNT,
                                                    PERIPHERAL_LINK_COUNT,
                                                    &ble_enable_params);
    APP_ERROR_CHECK(err_code);

#ifdef BLE_DFU_APP_SUPPORT
    ble_enable_params.gatts_enable_params.service_changed = 1;
#endif // BLE_DFU_APP_SUPPORT
    //Check the ram settings against the used number of links
    CHECK_RAM_START_ADDR(CENTRAL_LINK_COUNT,PERIPHERAL_LINK_COUNT);

    // Enable BLE stack.
    err_code = softdevice_enable(&ble_enable_params);
    APP_ERROR_CHECK(err_code);

    // Register with the SoftDevice handler module for BLE events.
    err_code = softdevice_ble_evt_handler_set(ble_evt_dispatch);
    APP_ERROR_CHECK(err_code);

    // Register with the SoftDevice handler module for BLE events.
    err_code = softdevice_sys_evt_handler_set(sys_evt_dispatch);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling events from the BSP module.
 *
 * @param[in]   event   Event generated by button press.
 */
void bsp_event_handler(bsp_event_t event)
{
    uint32_t err_code;
    switch (event)
    {
        case BSP_EVENT_SLEEP:
            sleep_mode_enter();
            break;

        case BSP_EVENT_DISCONNECT:
            err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            if (err_code != NRF_ERROR_INVALID_STATE)
            {
                APP_ERROR_CHECK(err_code);
            }
            break;

        case BSP_EVENT_WHITELIST_OFF:
            err_code = ble_advertising_restart_without_whitelist();
            if (err_code != NRF_ERROR_INVALID_STATE)
            {
                APP_ERROR_CHECK(err_code);
            }
            break;
				
				case  BSP_EVENT_KEY_2:
					LEDS_INVERT(BSP_LED_3_MASK);     
				  break;
							
        default:
            break;
    }
}


/**@brief Function for handling the Device Manager events.
 *
 * @param[in] p_evt  Data associated to the device manager event.
 */
static uint32_t device_manager_evt_handler(dm_handle_t const * p_handle,
                                           dm_event_t const  * p_event,
                                           ret_code_t        event_result)
{
    APP_ERROR_CHECK(event_result);

#ifdef BLE_DFU_APP_SUPPORT
    if (p_event->event_id == DM_EVT_LINK_SECURED)
    {
        app_context_load(p_handle);
    }
#endif // BLE_DFU_APP_SUPPORT

    return NRF_SUCCESS;
}


/**@brief Function for the Device Manager initialization.
 *
 * @param[in] erase_bonds  Indicates whether bonding information should be cleared from
 *                         persistent storage during initialization of the Device Manager.
 */
static void device_manager_init(bool erase_bonds)
{
    uint32_t               err_code;
    dm_init_param_t        init_param = {.clear_persistent_data = erase_bonds};
    dm_application_param_t register_param;

    // Initialize persistent storage module.
    err_code = pstorage_init();
    APP_ERROR_CHECK(err_code);

    err_code = dm_init(&init_param);
    APP_ERROR_CHECK(err_code);

    memset(&register_param.sec_param, 0, sizeof(ble_gap_sec_params_t));

    register_param.sec_param.bond         = SEC_PARAM_BOND;
    register_param.sec_param.mitm         = SEC_PARAM_MITM;
    register_param.sec_param.lesc         = SEC_PARAM_LESC;
    register_param.sec_param.keypress     = SEC_PARAM_KEYPRESS;
    register_param.sec_param.io_caps      = SEC_PARAM_IO_CAPABILITIES;
    register_param.sec_param.oob          = SEC_PARAM_OOB;
    register_param.sec_param.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
    register_param.sec_param.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
    register_param.evt_handler            = device_manager_evt_handler;
    register_param.service_type           = DM_PROTOCOL_CNTXT_GATT_SRVR_ID;

    err_code = dm_register(&m_app_handle, &register_param);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the Advertising functionality.
 */
static void advertising_init(void)
{
    uint32_t      err_code;
    ble_advdata_t advdata;

    // Build advertising data struct to pass into @ref ble_advertising_init.
    memset(&advdata, 0, sizeof(advdata));

    advdata.name_type               = BLE_ADVDATA_FULL_NAME;
    advdata.include_appearance      = true;
    advdata.flags                   = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    advdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    advdata.uuids_complete.p_uuids  = m_adv_uuids;

    ble_adv_modes_config_t options = {0};
    options.ble_adv_fast_enabled  = BLE_ADV_FAST_ENABLED;
    options.ble_adv_fast_interval = APP_ADV_INTERVAL;
    options.ble_adv_fast_timeout  = APP_ADV_TIMEOUT_IN_SECONDS;

    err_code = ble_advertising_init(&advdata, NULL, &options, on_adv_evt, NULL);
    APP_ERROR_CHECK(err_code);
}

void spi_event_handler(nrf_drv_spi_evt_t const * p_event)
{
    spi_xfer_done = true;
}


/**@brief Function for initializing buttons and leds.
 *
 * @param[out] p_erase_bonds  Will be true if the clear bonding button was pressed to wake the application up.
 */
static void ctrl_gpio_pin_init(bool * p_erase_bonds)
{
//    bsp_event_t startup_event;
//    uint32_t err_code = bsp_init(BSP_INIT_LED | BUTTON_2,  //| BSP_INIT_BUTTONS 
//                                 APP_TIMER_TICKS(100, APP_TIMER_PRESCALER),  //100ms
//                                  bsp_event_handler);
//	  APP_ERROR_CHECK(err_code);	
//	  LEDS_CONFIGURE(LEDS_MASK);  
//    err_code = bsp_btn_ble_init(NULL, &startup_event); 
//    APP_ERROR_CHECK(err_code);
//    *p_erase_bonds = (startup_event == BSP_EVENT_CLEAR_BONDING_DATA);
	

	  nrf_gpio_cfg_output(SX1276_RST);
	  nrf_gpio_cfg_output(SX1276_NSS);
	
		nrf_gpio_cfg_output(SPI0_CONFIG_SCK_PIN); //init SPI's gpio
		nrf_gpio_cfg_output(SPI0_CONFIG_MOSI_PIN);
		nrf_gpio_cfg_input (SPI0_CONFIG_MISO_PIN,NRF_GPIO_PIN_NOPULL);	
		
	  nrf_gpio_cfg_input(SX1276_DIO0,NRF_GPIO_PIN_NOPULL);
	  nrf_gpio_cfg_output(SX1276_EXPA);  //Init PA
	  nrf_gpio_pin_set(SX1276_EXPA);
	
	  nrf_gpio_cfg_output(24);  //VCC Power
	  nrf_gpio_pin_set(24);
		nrf_gpio_cfg_output(6);
	  nrf_gpio_pin_set(6);
		
	  
	  nrf_delay_ms(50);
	
	  nrf_drv_spi_config_t spi_config = NRF_DRV_SPI_DEFAULT_CONFIG(SPI_INSTANCE);
    spi_config.ss_pin = SPI_CS_PIN;
    APP_ERROR_CHECK(nrf_drv_spi_init(&spi, &spi_config, spi_event_handler));
}


void SetRadioChannel(uint8_t ch)
{
  if (ch>=1 && ch<=10)
  {
    SX1276LoRaSetRFFrequency(RadioChannelFrequencies[ch-1]);
  }
}


void SX1276_Rx_IT(void)
{
  if (rfTxBusyFlag)
  {
    return;
  }
  else
  { 
    SX1276LoRaStartRx();
    SX1276LoRaProcess();
  }
}
	
void ProcessRadioPacket(uint8_t* buf, uint32_t len)
{
	LocationStatus  locationStatus;
	Dispatch        dispatch;  
	
  uint8_t   encodeBuf[128] = {0};
  uint32_t  encodeLen = 0;
  uint16_t  crc16_local = 0;
  uint8_t   id_read;
  Dispatch  dispatchRepeater;
  uint8_t   repeaterID = 0;
  uint8_t   repeaterTTL[7] = {0};
  uint8_t   repeaterValid[7] = {0};
  uint8_t   repeaterBattery[7] = {0};
  uint32_t  radioStrength_once = 0;
	extern 	  double   RxPacketRssiValue;
	
	if(len > 4)
	{
		switch(buf[0])
		{
			case LocationStatusType:     //sx1276 -->  52832
				if (len == sizeof(LocationStatus))
        {
          memcpy(&locationStatus, buf, sizeof(LocationStatus));
          crc16_local = CRC16(buf, 0, len-2);
          if (crc16_local == locationStatus.CRC16)
          {
            id_read = locationStatus.Head & 0x00FF;
            {
              if (locationStatus.Timestamp == preDataTimestamp[id_read])
              {
              }
              else
              {
                //locationStatus.Head = id;
                locationStatus.CRC16 = CRC16((uint8_t*)&locationStatus, 0, sizeof(LocationStatus)-2);
                encodeLen = EncodePacket((uint8_t*)&locationStatus, encodeBuf, sizeof(LocationStatus));
								//opt
								spider_tunnel_put(&m_spider_tunnel, encodeBuf,encodeLen);
								
								if (id_read>=1 && id_read<=12)
                {
                  // Update the PASS timeout count for dispatch status
                  passValidTimeout[id_read-1] = 0;
                }
                preDataTimestamp[id_read] = locationStatus.Timestamp;
								
							}
						}
						if (RxPacketRssiValue > -22.0f)
            {
              radioStrength_once = 99;
            }
            else if (RxPacketRssiValue < -121.0f)
            {
              radioStrength_once = 0;
            }
            else
            {
              radioStrength_once = RxPacketRssiValue + 121.0f;
            }
           // sprintf(encodeBuf, "%u ID: %u LocationStatus RSSI:%u\r\n", currentTick-startTick, id_read, radioStrength_once);
           // UART4_Transmit(encodeBuf, strlen(encodeBuf));
						
					}
				}
			case	DispatchType:
				if(len == sizeof(Dispatch))
				{
				  memcpy(&dispatchRepeater, buf, sizeof(Dispatch));
          crc16_local = CRC16(buf, 0, len-2);
          if (crc16_local == dispatchRepeater.CRC16)
          {
            encodeLen = EncodePacket((uint8_t*)&dispatchRepeater, encodeBuf, sizeof(Dispatch));
            //UART1_Transmit(encodeBuf, encodeLen);
						
				//		spider_tunnel_put(&m_spider_tunnel, encodeBuf,encodeLen);
						
            for(int i= 0;i < 7;i++)
            {
              repeaterValid[i] = (dispatchRepeater.RepeaterEchoValid &  (2 << i)) >> (i + 1);
              repeaterTTL[i]   = (dispatchRepeater.RepeaterTTL & (0x0F << (i * 4))) >> (4 * i);
              repeaterBattery[i] = (dispatchRepeater.Battery & (0xF0 << (i * 4))) >> (4 * (i + 1));
            }            
            repeaterID = (dispatchRepeater.Head & 0xF00) >> 8;
            if (repeaterID != 0)
            {
              for (int i=0; i<7; i++)
              {
                if (repeaterValid[i] && (repeaterTTL[i] > repeaterTTLMax[i]))
                {
                  repeaterTTLMax[i] = repeaterTTL[i];
                  repeaterValidTimeout[i] = 0;
                  dispatch.RepeaterEchoValid |= ((0x02)<<i);
                  dispatch.RepeaterTTL &= (~(0x0F<<(4*i)));
                  dispatch.RepeaterTTL |= ((repeaterTTL[i])<<(4*i));
                  dispatch.Battery &= (~(0xF0<<(4*i)));
                  dispatch.Battery |= (repeaterBattery[i]<<(4*(i+1)));
                }
              }
            }
          }
          if (RxPacketRssiValue > -22.0f)
          {
            radioStrength_once = 99;
          }
          else if (RxPacketRssiValue < -121.0f)
          {
            radioStrength_once = 0;
          }
          else
          {
            radioStrength_once = RxPacketRssiValue + 121.0f;
          }
					
//          sprintf(encodeBuf, "%u ID: %u Dispatch TTL:%u 0x%02X 0x%04X RSSI:%u\r\n", currentTick-startTick, repeaterID,
//                  (dispatchRepeater.Head&0xC0)>>6, dispatchRepeater.Status, dispatchRepeater.RepeaterTTL, radioStrength_once);
//          UART4_Transmit(encodeBuf, strlen(encodeBuf));
				}	
		}
	}
}


//void confirme_data(void)
//{
//	  uint32_t  encodeLen = 0;
//	  uint8_t   encodeBuf[128] = {0};
//		
//		if(!channel_confirmed)
//		{
//			request.Type = RequestType;
//			request.Cmd = CMD_CHANNEL;
//			request.Parameter = 0;
//			request.CRC16 = CRC16((uint8_t*)&request, 0, sizeof(Request)-2);
//			encodeLen = EncodePacket((uint8_t*)&request, encodeBuf, sizeof(Request));
//		//	UART1_Transmit(encodeBuf, encodeLen);
//		}
//		if(!total_number_confirmed)
//		{
//			request.Type = RequestType;
//			request.Cmd = CMD_TOTAL_NUMBER;
//			request.Parameter = 0;
//			request.CRC16 = CRC16((uint8_t*)&request, 0, sizeof(Request)-2);
//			encodeLen = EncodePacket((uint8_t*)&request, encodeBuf, sizeof(Request));
//			//UART1_Transmit(encodeBuf, encodeLen);
//		}
//}

void dispatch_task(void)
{
	    uint8_t   id = 0;
	    uint32_t  encodeLen = 0;
	    uint8_t   encodeBuf[128] = {0};
      uint8_t   SX1276Buf[128] = {0};
	
	    memset(repeaterTTLMax, 0, sizeof(repeaterTTLMax));
      if (dispatchCmdCount)
      {
        dispatchCmdCount--;
      }
      else
      {
        dispatch.Cmd = 0;
        dispatch.Head = 0x7000;
      }
    //  dispatch.Timestamp = time(0);
      dispatch.Status = 0x00;
      for (id=0; id<7; id++)
      {
        if ( repeaterValidTimeout[id] > 2)
        {
          // Mark repeater Valid to 0                                                                      
          dispatch.RepeaterEchoValid &= (~(0x02<<id));
          dispatch.RepeaterTTL &= (~(0x0F<<(4*id)));
        }
        else
        {
          dispatch.RepeaterEchoValid |= (0x02<<id);
        }
        
        if (repeaterValidTimeout[id] < 255)
        {
          repeaterValidTimeout[id]++;
        }
      }
      if (referenceValidTimeout > 2)
      {
        dispatch.Status &= (~0x01);
        gpsPressure.Head &= (~0x0c);
      }
      else
      {
        dispatch.Status |= 0x01;
      }
			
      if (referenceValidTimeout < 255)
      {
        referenceValidTimeout++;
      }
			
      for (id=0; id<12; id++)
      {
        if (passValidTimeout[id] > 2)
        {
          dispatch.Status &= (~(1<<(1+id)));
        }
        else
        {
          dispatch.Status |= (1<<(1+id));
        }
        if (passValidTimeout[id] < 255)
        {
          passValidTimeout[id]++;
        }
      }
      dispatch.ReferencePressure = pressure;
      dispatch.ReferenceTemperature = (temperature + 0.5f);
      dispatch.CRC16 = CRC16((uint8_t*)&dispatch, 0, sizeof(Dispatch)-2);
      memcpy(SX1276Buf,(uint8_t *)&dispatch, sizeof(Dispatch));
      encodeLen = EncodePacket((uint8_t*)&dispatch, encodeBuf, sizeof(Dispatch));
    	
			SX1276SetTxPacket(SX1276Buf, sizeof(Dispatch));
	    SX1276LoRaProcess();
			nrf_delay_ms(40);
			SX1276_Rx_IT();		
		
}


void  local_set(void)
{
	   //init sx1276	
	  SX1276Init();
    SX1276LoRaSetPreambleLength(6);
    SX1276LoRaSetLowDatarateOptimize(false);
    SetRadioChannel(channel);  
    SX1276_Rx_IT();
    radioReady = 1;
	
	
    // Init dispatch
		dispatch.Type = DispatchType;
		//response.Type = ResponseType;
		dispatch.Head = 0x7000;
		dispatch.TotalNumber = deviceCount;
		dispatch.TimeGroup = 0; // Time Group is always 0 if sent from controller
		dispatch.Cmd = 0x00;
	
	
		referenceValidTimeout = 255;
		for (id=0; id<12; id++)
		{
			passValidTimeout[id] = 255;
		}                                                  
		for (id=0; id<7; id++)
		{
			repeaterValidTimeout[id] = 255;
		}
}

static bool sensor_init(void)
{
  twi_master_init();
  if (bmp280_init(BMP280_ADDRESS))
  {
    bmp280_readCalibration();
    bmp280_config();
    return true;
  }
  return false;
}
void get_bmp280_data(void)
{
  static float smoothedP = 100000.0;
  static float smoothedT = 100000.0;
  static uint8_t Press_firstValue = 1;
  static float sea_press = 1050.0f;
  static int32_t T, P = 0.0;
  static int32_t adc_T, adc_P = 0;
  static int32_t celciusX100;
  float alpha = 0.03;

  bmp280_getUnPT(&adc_P, &adc_T);
  T = bmp280_compensate_T_int32(adc_T);
  P = bmp280_compensate_P_int64(adc_P);

  if (Press_firstValue)
  {
    smoothedP = 0.01*P/256.0;
    smoothedT = 0.01*T;
    Press_firstValue = 0;
  }
  else
  {
    smoothedT = smoothedT*(1-alpha) +0.01f*alpha*T;
    smoothedP = smoothedP*(1-alpha) + 0.01f*alpha*P/256.0;
  }
  pressure     = smoothedP;
  temperature  = smoothedT;
}
	



/**@brief Function for application main entry.
 */
int main(void)
{
    uint32_t  err_code;
    bool      erase_bonds;
	  uint8_t   rfLoRaState = RFLR_STATE_IDLE;
	  uint8_t   compare_receive_flag = 0;
    uint8_t   rx_buf[128] = {0};
    uint16_t  rx_len = 0;
		
		app_fifo_init(&ble_rx_fifo, ble_rx_buff, 256);
		
    // Initialize.
    ctrl_gpio_pin_init(&erase_bonds);			
    app_trace_init();	
    timers_init();
    ble_stack_init();
    device_manager_init(erase_bonds);
		
		flash_init();
    gap_params_init();
    advertising_init();
    services_init();
    conn_params_init();
	 
		sensor_init();	
		
		
		APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
		
	  local_set();

    // Start execution. 
    application_timers_start();
    err_code = ble_advertising_start(BLE_ADV_MODE_FAST);
    APP_ERROR_CHECK(err_code);
		
		
    for (;;)
    {   
	  	  app_sched_execute();
        power_manage();
			   					
			  if(one_ms_task_flag == 1)
				{
					one_ms_task_flag = 0;
					if(nrf_gpio_pin_read(SX1276_DIO0) == 1)
					{
						compare_receive_flag = 1;
					}
				}
				
				if(one_sec_task_flag == 1)
				{
					one_sec_task_flag = 0;
					dispatch_task();
				}
				if(get_bmp280_flag == 1)
				{
					get_bmp280_flag = 0;
					get_bmp280_data();
				}
					

			
			  if(compare_receive_flag == 1)
				{
					  compare_receive_flag = 0;
						rfLoRaState = SX1276LoRaGetRFState();
						if(rfLoRaState == RFLR_STATE_RX_RUNNING )
						{
							SX1276LoRaProcess();
							SX1276LoRaProcess();
							rfLoRaState = SX1276LoRaGetRFState();
							if (RFLR_STATE_RX_RUNNING == rfLoRaState)
							{
							//  rxDoneTick = osKernelSysTick();
								SX1276LoRaGetRxPacket(rx_buf, &rx_len);	
								ProcessRadioPacket(rx_buf, rx_len);								
								
							}
							else if (RFLR_STATE_RX_INIT == rfLoRaState) // RX TimeOut
							{
								SX1276LoRaProcess();
							}
						}				
			  }
				

    }
}
