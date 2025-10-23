/*******************************************************************************
* File Name: app_bt_gatt_handler.c
*
* Description: This file consists of the function defintions that are
*              necessary for developing the Bluetooth LE applications with GATT Server
*              callbacks.
*
* Related Document: See README.md
*
*
 *********************************************************************************
 Copyright 2020-2025, Cypress Semiconductor Corporation (an Infineon company) or
 an affiliate of Cypress Semiconductor Corporation.  All rights reserved.

 This software, including source code, documentation and related
 materials ("Software") is owned by Cypress Semiconductor Corporation
 or one of its affiliates ("Cypress") and is protected by and subject to
 worldwide patent protection (United States and foreign),
 United States copyright laws and international treaty provisions.
 Therefore, you may use this Software only as provided in the license
 agreement accompanying the software package from which you
 obtained this Software ("EULA").
 If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 non-transferable license to copy, modify, and compile the Software
 source code solely for use in connection with Cypress's
 integrated circuit products.  Any reproduction, modification, translation,
 compilation, or representation of this Software except as specified
 above is prohibited without the express written permission of Cypress.

 Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 reserves the right to make changes to the Software without notice. Cypress
 does not assume any liability arising out of the application or use of the
 Software or any product or circuit described in the Software. Cypress does
 not authorize its products for use in any products where a malfunction or
 failure of the Cypress product may reasonably be expected to result in
 significant property damage, injury or death ("High Risk Product"). By
 including Cypress's product in a High Risk Product, the manufacturer
 of such system or application assumes all risk of such use and in doing
 so agrees to indemnify Cypress against all liability.
 *******************************************************************************/

/* *****************************************************************************
 *                              INCLUDES
 * ****************************************************************************/
#include "app_bt_gatt_handler.h"
#include "app_bt_utils.h"
#include "app_data_transfer.h"
#include "GeneratedSource/cycfg_gatt_db.h"
#include "cyhal_gpio.h"
#include "cybt_platform_trace.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_l2c.h"
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <string.h>
#include <timers.h>

/*******************************************************************************
 *                              FUNCTION DECLARATIONS
 ******************************************************************************/

static void* app_alloc_buffer(int len);

static void app_free_buffer(uint8_t *p_event_data);

typedef void (*pfn_free_buffer_t)(uint8_t *);

wiced_bt_gatt_status_t
app_gatt_read_by_type_handler(uint16_t conn_id,
                                wiced_bt_gatt_opcode_t opcode,
                                wiced_bt_gatt_read_by_type_t *p_read_req,
                                uint16_t len_requested,
                                uint16_t *p_error_handle);

/* *****************************************************************************
 *                              FUNCTION DEFINITIONS
 * ****************************************************************************/
/*
 Function Name:
 app_bt_gatt_event_callback

 Function Description:
 @brief  This Function handles the all the GATT events - GATT Event Handler

 @param event            Bluetooth LE GATT event type
 @param p_event_data     Pointer to Bluetooth LE GATT event data

 @return wiced_bt_gatt_status_t  Bluetooth LE GATT status
 */
wiced_bt_gatt_status_t
app_bt_gatt_event_callback(wiced_bt_gatt_evt_t event,
                           wiced_bt_gatt_event_data_t *p_event_data)
{

    wiced_bt_gatt_status_t gatt_status = WICED_BT_GATT_ERROR;
    wiced_bt_gatt_attribute_request_t *p_attr_req = NULL;

    uint16_t error_handle = 0;

    switch (event)
    {

    case GATT_CONNECTION_STATUS_EVT:
        gatt_status = app_gatt_connect_handler(&p_event_data->connection_status);
        break;

    case GATT_ATTRIBUTE_REQUEST_EVT:
        p_attr_req = &p_event_data->attribute_request;
        gatt_status = app_gatts_attr_req_handler(p_attr_req,
                                                 &error_handle);

        if(gatt_status != WICED_BT_GATT_SUCCESS)
        {
           wiced_bt_gatt_server_send_error_rsp(p_attr_req->conn_id,
                                               p_attr_req->opcode,
                                               error_handle,
                                               gatt_status);
        }

        break;

    case GATT_GET_RESPONSE_BUFFER_EVT:
    {
        wiced_bt_gatt_buffer_request_t *p_buf_req = &p_event_data->buffer_request;
        /* Allocate buffer for GATT response - don't print to avoid console spam */
        p_buf_req->buffer.p_app_rsp_buffer = app_alloc_buffer(p_buf_req->len_requested);
        p_buf_req->buffer.p_app_ctxt = (void *)app_free_buffer;
        gatt_status = WICED_BT_GATT_SUCCESS;
    }
        break;

    case GATT_APP_BUFFER_TRANSMITTED_EVT:
    {
        pfn_free_buffer_t pfn_free;
        pfn_free = (pfn_free_buffer_t)p_event_data->buffer_xmitted.p_app_ctxt;
        /* If the buffer is dynamic, the context will point to a function to
         * free it.
         */
        if (pfn_free)
        {
            pfn_free(p_event_data->buffer_xmitted.p_app_data);
        }

        /* Notify data transfer module that a notification was transmitted
         * This implements credit-based flow control to prevent buffer overflow */
        app_data_transfer_notification_sent();

        gatt_status = WICED_BT_GATT_SUCCESS;
    }
       break;

    case GATT_CONGESTION_EVT:
    {
        /* BLE stack congestion status changed
         * congested = true means stack buffers are full, slow down
         * congested = false means stack buffers have room, can speed up
         * Our adaptive flow control in app_data_transfer.c handles this
         * by tracking send failures and adjusting delay dynamically.
         */
        /* Don't print to avoid console spam - our send_chunk already handles this */
        gatt_status = WICED_BT_GATT_SUCCESS;
    }
        break;

    default:
        printf("Unhandled GATT Event %d", event);
        break;

    }

    return gatt_status;
}


/*
 Function Name:
 app_gatt_connect_handler

 Function Description:
 @brief  The callback function is invoked when GATT_CONNECTION_STATUS_EVT occurs
         in GATT Event handler function

 @param p_conn_status     Pointer to Bluetooth LE GATT connection status

 @return wiced_bt_gatt_status_t  Bluetooth LE GATT status
 */
wiced_bt_gatt_status_t
app_gatt_connect_handler(wiced_bt_gatt_connection_status_t *p_conn_status)
{

    wiced_result_t gatt_status = WICED_ERROR;

    if ((p_conn_status->connected) && (0 == app_bt_conn_id))
    {
        /* Device has connected */
        print_bd_address("\nConnected to BDA:", p_conn_status->bd_addr);
        printf("Connection ID: '%d'\n", p_conn_status->conn_id);

        cyhal_gpio_write(CONNECTION_LED, CYBSP_LED_STATE_ON);

        app_bt_conn_id  = p_conn_status->conn_id;
        gatt_status     = wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF,
                                                        BLE_ADDR_PUBLIC,
                                                        NULL);

        /* Request optimal connection parameters for data transfer */
        wiced_bt_ble_pref_conn_params_t conn_params = {
            .conn_interval_min = 12,  /* 12 * 1.25ms = 15ms */
            .conn_interval_max = 12,  /* 12 * 1.25ms = 15ms */
            .conn_latency = 0,        /* No latency */
            .conn_supervision_timeout = 200, /* 200 * 10ms = 2000ms */
            .min_ce_length = 0,
            .max_ce_length = 0
        };

        wiced_bool_t param_status = wiced_bt_l2cap_update_ble_conn_params(
            p_conn_status->bd_addr,
            &conn_params
        );

        printf("Requested connection parameters: interval=15ms, latency=0, timeout=2000ms\n");
        printf("   Request status: %s\n", param_status ? "SUCCESS" : "FAILED");

        /* Request LE 2M PHY for 2x speed (Bluetooth 5.0 feature) */
        wiced_bt_ble_phy_preferences_t phy_prefs;
        memset(&phy_prefs, 0, sizeof(phy_prefs));
        memcpy(phy_prefs.remote_bd_addr, p_conn_status->bd_addr, BD_ADDR_LEN);
        phy_prefs.tx_phys = BTM_BLE_PREFER_2M_PHY;  /* Prefer 2M for TX */
        phy_prefs.rx_phys = BTM_BLE_PREFER_2M_PHY;  /* Prefer 2M for RX */
        phy_prefs.phy_opts = BTM_BLE_PREFER_NO_LELR; /* No coded PHY */

        wiced_bt_dev_status_t phy_status = wiced_bt_ble_set_phy(&phy_prefs);
        printf("Requested LE 2M PHY (2x speed):\n");
        printf("   Request status: %s\n", phy_status == WICED_BT_SUCCESS ? "SUCCESS" : "FAILED");

        /* Request Data Length Extension for larger packets (up to 251 bytes)
         * With 2M PHY, the time will be automatically adjusted by the controller */
        wiced_bt_dev_status_t dle_status = wiced_bt_ble_set_data_packet_length(
            p_conn_status->bd_addr,
            251,    /* tx_pdu_length: Max TX packet length */
            2120    /* tx_time: Max TX time in microseconds (251 bytes @ 1Mbps) */
        );
        printf("Requested Data Length Extension (251 bytes):\n");
        printf("   Request status: %s\n", dle_status == WICED_BT_SUCCESS ? "SUCCESS" : "FAILED");

    }
    else
    {
        /* Device has disconnected */
        print_bd_address("\nDisconnected from BDA: ", p_conn_status->bd_addr);
        printf("Connection ID: '%d'\n", p_conn_status->conn_id);
        printf("\nReason for disconnection: \t%s\n",                        \
                        get_gatt_disconn_reason_name(p_conn_status->reason));

        cyhal_gpio_write(CONNECTION_LED, CYBSP_LED_STATE_OFF);

        /* Pause data transfer if active */
        app_data_transfer_pause();

        /* Handle the disconnection */
        app_bt_conn_id  = 0;

        /*
         * Reset the CCCD value so that on a reconnect CCCD (notifications)
         * will be off
         */
        app_ess_temperature_client_char_config[0] = 0;
        gatt_status = wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH,
                                                    BLE_ADDR_PUBLIC,
                                                    NULL);
    }

    return gatt_status;
}

/*
 Function Name:
 app_gatts_attr_req_handler

 Function Description:
 @brief  The callback function is invoked when GATT_ATTRIBUTE_REQUEST_EVT occurs
         in GATT Event handler function. GATT Server Event Callback function.

 @param type  Pointer to GATT Attribute request

 @return wiced_bt_gatt_status_t  Bluetooth LE GATT gatt_status
 */
wiced_bt_gatt_status_t
app_gatts_attr_req_handler(wiced_bt_gatt_attribute_request_t *p_attr_req, 
                           uint16_t *p_error_handle)
{

    wiced_bt_gatt_status_t gatt_status = WICED_BT_GATT_ERROR;

    switch (p_attr_req->opcode)
    {

        case GATT_REQ_READ:
        case GATT_REQ_READ_BLOB:
             gatt_status = app_gatt_attr_read_handler(p_attr_req->conn_id,
                                                      p_attr_req->opcode,
                                                     &p_attr_req->data.read_req,
                                                      p_attr_req->len_requested,
                                                      p_error_handle);
             break;

        case GATT_REQ_WRITE:
        case GATT_CMD_WRITE:
        case GATT_CMD_SIGNED_WRITE:
             gatt_status = app_gatt_attr_write_handler(p_attr_req->opcode,
                                                       &p_attr_req->data.write_req,
                                                       p_attr_req->len_requested,
                                                       p_error_handle);
             if ((p_attr_req->opcode == GATT_REQ_WRITE) && (gatt_status == WICED_BT_GATT_SUCCESS))
             {
                 wiced_bt_gatt_write_req_t *p_write_request = &p_attr_req->data.write_req;
                 wiced_bt_gatt_server_send_write_rsp(p_attr_req->conn_id, p_attr_req->opcode,
                                                     p_write_request->handle);
             }
             break;

        case GATT_REQ_MTU:
            /* This is the response for GATT MTU exchange and MTU size is set
             * in the BT-Configurator.
             */
            {
                uint16_t negotiated_mtu = (p_attr_req->data.remote_mtu < CY_BT_MTU_SIZE) ?
                                           p_attr_req->data.remote_mtu : CY_BT_MTU_SIZE;

                printf("MTU Exchange: Remote=%d, Local=%d, Negotiated=%d\n",
                       p_attr_req->data.remote_mtu, CY_BT_MTU_SIZE, negotiated_mtu);

                /* Inform data transfer module of negotiated MTU */
                app_data_transfer_set_mtu(negotiated_mtu);

                gatt_status = wiced_bt_gatt_server_send_mtu_rsp(p_attr_req->conn_id,
                                                                p_attr_req->data.remote_mtu,
                                                                CY_BT_MTU_SIZE);
            }
            break;

        case GATT_HANDLE_VALUE_NOTIF:
            /* Notification ACK - don't print to avoid flooding console */
            break;

        case GATT_REQ_READ_BY_TYPE:
            gatt_status = app_gatt_read_by_type_handler(p_attr_req->conn_id,
                                                               p_attr_req->opcode,
                                                               &p_attr_req->data.read_by_type,
                                                               p_attr_req->len_requested,
                                                               p_error_handle);
            break;

        default:
            printf("ERROR: Unhandled GATT Connection Request case: %d\n", p_attr_req->opcode);
            break;

    }

    return gatt_status;
}

/*
 Function Name:
 app_gatt_attr_read_handler

 Function Description:
 @brief  The function is invoked when GATT_REQ_READ is received from the
         client device and is invoked by GATT Server Event Callback function.
         This handles "Read Requests" received from Client device

@param conn_id       Connection ID
@param opcode        Bluetooth LE GATT request type opcode
@param p_read_req    Pointer to read request containing the handle to read
@param len_req       length of data requested

 @return wiced_bt_gatt_status_t  Bluetooth LE GATT status
 */
wiced_bt_gatt_status_t
app_gatt_attr_read_handler( uint16_t conn_id,
                            wiced_bt_gatt_opcode_t opcode,
                            wiced_bt_gatt_read_t *p_read_req,
                            uint16_t len_req,
                            uint16_t *p_error_handle)
{
    wiced_bt_gatt_status_t gatt_status = WICED_BT_GATT_INVALID_HANDLE;
    int32_t index = 0;
    uint16_t len_to_send = 0;
    *p_error_handle = p_read_req->handle;

    /* Validate the length of the attribute and read from the attribute */
    index = app_get_attr_index_by_handle((p_read_req->handle));
    if (INVALID_ATT_TBL_INDEX != index)
    {
        len_to_send = app_gatt_db_ext_attr_tbl[index].cur_len - p_read_req->offset;
        if(len_to_send <= 0)
        {
            return WICED_BT_GATT_INVALID_ATTR_LEN;
        }

        if(len_req < len_to_send)
        {
            len_to_send = len_req;
        }

        /*
         * Set the pv_app_context parameter to NULL, since we don't want to free
         * app_gatt_db_ext_attr_tbl[index].p_data on transmit complete
         */
        gatt_status = wiced_bt_gatt_server_send_read_handle_rsp(conn_id,
                                                                opcode,
                                                                len_to_send,
                                        app_gatt_db_ext_attr_tbl[index].p_data,
                                                                NULL);
    }
    else
    {
        return WICED_BT_GATT_INVALID_HANDLE;
    }

    return (gatt_status);
}


/*
 Function Name:
 app_gatt_attr_write_handler

 Function Description:
 @brief  The function is invoked when GATT_REQ_WRITE is received from the
         client device and is invoked GATT Server Event Callback function. This
         handles "Write Requests" received from Client device.
 @param conn_id       Connection ID
 @param opcode        Bluetooth LE GATT request type opcode
 @param p_write_req   Pointer to Bluetooth LE GATT write request
 @param len_req       length of data requested

 @return wiced_bt_gatt_status_t  Bluetooth LE GATT status
 */
wiced_bt_gatt_status_t
app_gatt_attr_write_handler(wiced_bt_gatt_opcode_t opcode,
                            wiced_bt_gatt_write_req_t *p_write_req,
                            uint16_t len_req,
                            uint16_t *p_error_handle)
{
    wiced_bt_gatt_status_t gatt_status = WICED_BT_GATT_INVALID_HANDLE;
    int index = 0;
    *p_error_handle = p_write_req->handle;

    /* Binary search of handles is done; Make sure the handles are sorted */
    index = app_get_attr_index_by_handle(p_write_req->handle);
    if(INVALID_ATT_TBL_INDEX == index)
    {
        printf("Invalid ATT TBL Index : %d\n", index);
        return gatt_status;
    }

    gatt_status = app_set_gatt_attr_value(  p_write_req->handle,
                                            p_write_req->p_val,
                                            p_write_req->val_len);
    if( WICED_BT_GATT_SUCCESS != gatt_status )
    {
        printf("WARNING: GATT set attr status 0x%x\n", gatt_status);
    }

    return (gatt_status);

}

/**
 * Function Name:
 * app_bt_gatt_req_read_by_type_handler
 *
 * Function Description:
 * @brief  Process read-by-type request from peer device
 *
 * @param conn_id       Connection ID
 * @param opcode        BLE GATT request type opcode
 * @param p_read_req    Pointer to read request containing the handle to read
 * @param len_requested length of data requested
 *
 * @return wiced_bt_gatt_status_t  BLE GATT status
 */
wiced_bt_gatt_status_t
app_gatt_read_by_type_handler(uint16_t conn_id,
                                wiced_bt_gatt_opcode_t opcode,
                                wiced_bt_gatt_read_by_type_t *p_read_req,
                                uint16_t len_requested,
                                uint16_t *p_error_handle)
{
    uint16_t    attr_handle = p_read_req->s_handle;
    uint8_t     *p_rsp = app_alloc_buffer(len_requested);
    uint8_t     pair_len = 0;
    int         index = 0;
    int         used = 0;
    int         filled = 0;

    /* Don't print to avoid console spam during service discovery */
    if (p_rsp == NULL)
    {
        printf("OOM, len_requested: %d !! \r\n",len_requested);
        return WICED_BT_GATT_INSUF_RESOURCE;
    }

    /* Read by type returns all attributes of the specified type,
     * between the start and end handles */
    while (TRUE)
    {
        *p_error_handle = attr_handle;
        attr_handle = wiced_bt_gatt_find_handle_by_type(attr_handle,
                                                        p_read_req->e_handle,
                                                        &p_read_req->uuid);

        if (attr_handle == 0)
            break;

        index = app_get_attr_index_by_handle(attr_handle);
        if (INVALID_ATT_TBL_INDEX != index)
        {
            /* Build response - don't print to avoid console spam */
            filled = wiced_bt_gatt_put_read_by_type_rsp_in_stream( p_rsp + used,
                                                        len_requested - used,
                                                        &pair_len,
                                                        attr_handle,
                                        app_gatt_db_ext_attr_tbl[index].cur_len,
                                        app_gatt_db_ext_attr_tbl[index].p_data);
            if (filled == 0)
            {
                /* No more space in response buffer */
                break;
            }
            used += filled;
        }
        else
        {
            app_free_buffer(p_rsp);
            return WICED_BT_GATT_ERR_UNLIKELY;
        }
        /* Increment starting handle for next search to one past current */
        attr_handle++;
    } // End of adding the data to the stream

    if (used == 0)
    {
       printf("attr not found  start_handle: 0x%04x  end_handle: 0x%04x  Type: 0x%04x\r\n",
               p_read_req->s_handle, p_read_req->e_handle, p_read_req->uuid.uu.uuid16);
        app_free_buffer(p_rsp);
        return WICED_BT_GATT_INVALID_HANDLE;
    }

    /* Send the response */
    wiced_bt_gatt_server_send_read_by_type_rsp( conn_id,
                                                opcode,
                                                pair_len,
                                                used,
                                                p_rsp,
                            (wiced_bt_gatt_app_context_t)app_free_buffer);

    return WICED_BT_GATT_SUCCESS;
}
/*
 Function Name:
 app_bt_set_value

 Function Description:
 @brief  The function is invoked by app_bt_write_handler to set a value
         to GATT DB.

 @param attr_handle  GATT attribute handle
 @param p_val        Pointer to Bluetooth LE GATT write request value
 @param len          length of GATT write request

 @return wiced_bt_gatt_status_t  Bluetooth LE GATT status
 */
wiced_bt_gatt_status_t app_set_gatt_attr_value(uint16_t attr_handle,
                                               uint8_t *p_val,
                                               uint16_t len)
{
    wiced_bt_gatt_status_t gatt_status = WICED_BT_GATT_INVALID_HANDLE;
      /* Check for a matching handle entry */
      if (HDLD_ESS_TEMPERATURE_CLIENT_CHAR_CONFIG == attr_handle)
      {
          /* Verify that size constraints have been met */
          if (app_ess_temperature_client_char_config_len >= len)
          {
              /* Value fits within the supplied buffer; copy over the value */
              memcpy(app_ess_temperature_client_char_config,
                     p_val,
                     len);

              gatt_status = WICED_BT_GATT_SUCCESS;
          }
          else
          {
              /* Value to write does not meet size constraints */
              gatt_status = WICED_BT_GATT_INVALID_ATTR_LEN;
          }
      }
      /* Data Transfer Service - Data Block CCCD */
      else if (HDLD_DATA_TRANSFER_SERVICE_DATA_BLOCK_CLIENT_CHAR_CONFIG == attr_handle)
      {
          if (len == 2)
          {
              uint16_t cccd_value = p_val[0] | (p_val[1] << 8);
              bool notifications_enabled = (cccd_value & GATT_CLIENT_CONFIG_NOTIFICATION) != 0;

              app_data_transfer_cccd_write_handler(app_bt_conn_id, notifications_enabled);

              gatt_status = WICED_BT_GATT_SUCCESS;
          }
          else
          {
              gatt_status = WICED_BT_GATT_INVALID_ATTR_LEN;
          }
      }
      /* Data Transfer Service - Control Characteristic */
      else if (HDLC_DATA_TRANSFER_SERVICE_CONTROL_VALUE == attr_handle)
      {
          /* Route to data transfer control handler */
          wiced_bt_gatt_write_req_t write_req;
          write_req.handle = attr_handle;
          write_req.p_val = p_val;
          write_req.val_len = len;
          write_req.offset = 0;

          app_data_transfer_control_write_handler(app_bt_conn_id, &write_req);

          gatt_status = WICED_BT_GATT_SUCCESS;
      }

  return (gatt_status);
}

/**
 * @brief This function returns the corresponding index for the respective
 *        attribute handle from the attribute table. Please ensure that GATT DB
 *        is sorted.
 *
 * @param attr_handle 16-bit attribute handle for the characteristics and descriptors
 * @return int32_t The index of the valid attribute handle otherwise
 *         INVALID_ATT_TBL_INDEX
 */
int32_t app_get_attr_index_by_handle(uint16_t attr_handle)
{

    uint16_t left = 0;
    uint16_t right = app_gatt_db_ext_attr_tbl_size;

    while(left <= right)
    {
        uint16_t mid = left + (right - left)/2;

        if(app_gatt_db_ext_attr_tbl[mid].handle == attr_handle)
        {
            return mid;
        }

        if(app_gatt_db_ext_attr_tbl[mid].handle < attr_handle)
        {
            left = mid + 1;
        }
        else
        {
            right = mid - 1;
        }
    }

    return INVALID_ATT_TBL_INDEX;

}

/*******************************************************************************
 * Function Name: app_free_buffer
 *******************************************************************************
 * Summary:
 *  This function frees up the memory buffer
 *
 *
 * Parameters:
 *  uint8_t *p_data: Pointer to the buffer to be free
 *
 ******************************************************************************/
static void app_free_buffer(uint8_t *p_buf)
{
    vPortFree(p_buf);
}


/*******************************************************************************
 * Function Name: app_alloc_buffer
 *******************************************************************************
 * Summary:
 *  This function allocates a memory buffer.
 *
 *
 * Parameters:
 *  int len: Length to allocate
 *
 ******************************************************************************/
static void* app_alloc_buffer(int len)
{
    return pvPortMalloc(len);
}

/* [] END OF FILE */
