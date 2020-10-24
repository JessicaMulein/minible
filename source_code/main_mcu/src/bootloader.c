/* 
 * This file is part of the Mooltipass Project (https://github.com/mooltipass).
 * Copyright (c) 2019 Stephan Mathieu
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <asf.h>
#include "mooltipass_graphics_bundle.h"
#include "smartcard_highlevel.h"
#include "smartcard_lowlevel.h"
#include "platform_defines.h"
#include "bearssl_block.h"
#include "driver_clocks.h"
#include "comms_aux_mcu.h"
#include "driver_timer.h"
#include "platform_io.h"
#include "bearssl_ec.h"
#include "custom_fs.h"
#include "dataflash.h"
#include "lis2hh12.h"
#include "dbflash.h"
#include "defines.h"
#include "sh1122.h"
#include "inputs.h"
#include "utils.h"
#include "fuses.h"
#include "main.h"
#include "dma.h"
/* Defines for flashing */
volatile uint32_t current_address = APP_START_ADDR;
/* Our oled & dataflash & dbflash descriptors */
sh1122_descriptor_t plat_oled_descriptor = {.sercom_pt = OLED_SERCOM, .dma_trigger_id = OLED_DMA_SERCOM_TX_TRIG, .sh1122_cs_pin_group = OLED_nCS_GROUP, .sh1122_cs_pin_mask = OLED_nCS_MASK, .sh1122_cd_pin_group = OLED_CD_GROUP, .sh1122_cd_pin_mask = OLED_CD_MASK};
spi_flash_descriptor_t dataflash_descriptor = {.sercom_pt = DATAFLASH_SERCOM, .cs_pin_group = DATAFLASH_nCS_GROUP, .cs_pin_mask = DATAFLASH_nCS_MASK};
spi_flash_descriptor_t dbflash_descriptor = {.sercom_pt = DBFLASH_SERCOM, .cs_pin_group = DBFLASH_nCS_GROUP, .cs_pin_mask = DBFLASH_nCS_MASK};
#ifdef DEVELOPER_FEATURES_ENABLED
BOOL special_dev_card_inserted = FALSE;
#endif
/* Our struct needed for flashing */
typedef struct
{
    union
    {
        uint8_t uint8_row[NVMCTRL_ROW_SIZE];
        uint16_t uint16_row[NVMCTRL_ROW_SIZE/2];
    };
} uint8_uint16_aligned_mcu_row;


/**
 * \brief Function to start the application.
 */
static void start_application(void)
{
    /* Pointer to the Application Section */
    void (*application_code_entry)(void);

    /* Rebase the Stack Pointer */
    __set_MSP(*(uint32_t *)APP_START_ADDR);

    /* Rebase the vector table base address */
    SCB->VTOR = ((uint32_t)APP_START_ADDR & SCB_VTOR_TBLOFF_Msk);

    /* Load the Reset Handler address of the application */
    application_code_entry = (void (*)(void))(unsigned *)(*(unsigned *)(APP_START_ADDR + 4));

    /* Jump to user Reset Handler in the application */
    application_code_entry();
}

/*! \fn     brick_main_mcu(void)
*   \brief  Nice way to brick the main MCU
*/
static void brick_main_mcu(void)
{
    /* Automatic flash write, disable caching */
    NVMCTRL->CTRLB.bit.MANW = 0;
    NVMCTRL->CTRLB.bit.CACHEDIS = 1;

    /* Erase all pages of internal memory */
    for (uint32_t current_flash_address = APP_START_ADDR; current_flash_address < FLASH_SIZE; current_flash_address += NVMCTRL_ROW_SIZE)
    {
        /* Erase complete row, composed of 4 pages */
        while ((NVMCTRL->INTFLAG.reg & NVMCTRL_INTFLAG_READY) == 0);
        NVMCTRL->ADDR.reg  = current_flash_address/2;
        NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMD_ER | NVMCTRL_CTRLA_CMDEX_KEY;
    }
}

/*! \fn     store_row_buffer_into_main_memory(uint32_t store_address, uint16_t* row_buffer)
*   \brief  Store a row buffer into main mcu memory
*   \param  store_address   Where to store in the main MCU memory
*   \param  row_buffer      A full row buffer
*/
static void store_row_buffer_into_main_memory(uint32_t store_address, uint16_t* row_buffer)
{
    /* Erase complete row, composed of 4 pages */
    while ((NVMCTRL->INTFLAG.reg & NVMCTRL_INTFLAG_READY) == 0);
    NVMCTRL->ADDR.reg  = store_address/2;
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMD_ER | NVMCTRL_CTRLA_CMDEX_KEY;
    
    /* Flash bytes */
    for (uint32_t j = 0; j < 4; j++)
    {
        /* Flash 4 consecutive pages */
        while ((NVMCTRL->INTFLAG.reg & NVMCTRL_INTFLAG_READY) == 0);
        for(uint32_t i = 0; i < NVMCTRL_ROW_SIZE/4; i+=2)
        {
            NVM_MEMORY[(store_address+j*(NVMCTRL_ROW_SIZE/4)+i)/2] = row_buffer[(j*(NVMCTRL_ROW_SIZE/4)+i)/2];
        }
    }    
}

/*! \fn     main(void)
*   \brief  Program Main
*/
int main(void)
{
    custom_fs_address_t current_data_flash_addr;                        // Current data flash address
    uint8_uint16_aligned_mcu_row row_to_be_flashed;                     // MCU Row of data to be flashed
    uint8_uint16_aligned_mcu_row bundle_data_b1;                        // First buffer for bundle data
    uint8_uint16_aligned_mcu_row bundle_data_b2;                        // Second buffer for bundle data
    uint8_t* available_data_buffer;                                     // Available buffer to receive data
    uint8_t* received_data_buffer;                                      // Buffer in which we received data
    uint16_t nb_fw_bytes_in_row_buffer = 0;                             // Number of fw bytes available in row buffer
    //uint8_t old_version_number[4];                                      // Old firmware version identifier
    //uint8_t new_version_number[4];                                      // New firmware version identifier

#if defined(PLAT_V7_SETUP)
    /* Mass production units get signed firmware updates */
    br_aes_ct_ctrcbc_keys bootloader_encryption_aes_context;            // The AES encryption context
    br_aes_ct_ctrcbc_keys bootloader_signing_aes_context;               // The AES signing context
    //uint8_t new_aes_key[AES_KEY_LENGTH/8];                              // New AES signing key
    uint8_t encryption_aes_key[AES_KEY_LENGTH/8];                       // AES encryption key
    uint8_t signing_aes_key[AES_KEY_LENGTH/8];                          // AES signing key
    uint8_t encryption_aes_iv[16];                                      // AES IV for encryption
    uint8_t cbc_mac_to_end_of_mcu_fpass[16];                            // CBCMAC until the end of fw at first pass
    uint8_t cur_cbc_mac[16];                                            // Currently computed CBCMAC val
    //BOOL aes_key_update_bool;                                           // Boolean specifying that we want to update the aes key
    
    /* Sanity checks */
    _Static_assert((W25Q16_FLASH_SIZE-START_OF_SIGNED_DATA_IN_DATA_FLASH) % 16 == 0, "CBCMAC address space isn't a multiple of block size");
    _Static_assert(sizeof(row_to_be_flashed) == sizeof(bundle_data_b2), "Diff buffer sizes, flashing logic needs to be redone");
    _Static_assert(sizeof(row_to_be_flashed) == NVMCTRL_ROW_SIZE, "Incorrect row_to_be_flashed size to flash main MCU row");
    _Static_assert(sizeof(row_to_be_flashed) % 16 == 0, "Bundle buffer size is not a multiple of block size");
    _Static_assert(sizeof(bundle_data_b2) % 16 == 0, "Bundle buffer size is not a multiple of block size");
    _Static_assert(sizeof(bundle_data_b1) % 16 == 0, "Bundle buffer size is not a multiple of block size");
    _Static_assert(sizeof(encryption_aes_iv) == 16, "Invalid IV buffer size");
    _Static_assert(sizeof(cbc_mac_to_end_of_mcu_fpass) == 16, "Invalid MAC buffer size");
    _Static_assert(sizeof(cur_cbc_mac) == 16, "Invalid MAC buffer size");
#endif
    
    /* Enable switch and 3V3 stepup, set no comms signal, leave some time for stepup powerup */
    platform_io_enable_switch();
    platform_io_init_no_comms_signal();
    DELAYMS_8M(100);
    
    /* Fuses not programmed, start application who will check them anyway */
    if (fuses_check_program(FALSE) != RETURN_OK)
    {
        start_application();
    }
    
    /* Initialize our settings system: should not returned failed as fuses are programmed for rwee */
    if (custom_fs_settings_init() != CUSTOM_FS_INIT_OK)
    {
        platform_io_disable_switch_and_die();
        while(1);
    }
    
    /* If no upgrade flag set, jump to application */
    if (custom_fs_settings_check_fw_upgrade_flag() == FALSE)
    {
        start_application();
    }
    
    /* Store the dataflash descriptor for our custom fs library */
    custom_fs_set_dataflash_descriptor(&dataflash_descriptor);
    
    /* Change the MCU main clock to 48MHz */
    clocks_start_48MDFLL();
    
    /* Initialize flash io ports */
    platform_io_init_flash_ports();
    
    /* Check for external flash presence */
    if (dataflash_check_presence(&dataflash_descriptor) == RETURN_NOK)
    {
        custom_fs_settings_clear_fw_upgrade_flag();
        start_application();
    }
    
    /* Custom file system initialization */
    custom_fs_init();
    
    /* Look for update file address */
    custom_fs_address_t fw_file_address;
    custom_fs_binfile_size_t fw_file_size;
    if (custom_fs_get_file_address(0, &fw_file_address, CUSTOM_FS_FW_UPDATE_TYPE) == RETURN_NOK)
    {
        /* If we couldn't find the update file */
        custom_fs_settings_clear_fw_upgrade_flag();
        start_application();
    }
    
    /* Read file size */
    custom_fs_read_from_flash((uint8_t*)&fw_file_size, fw_file_address, sizeof(fw_file_size));
    fw_file_address += sizeof(fw_file_size);
    
    /* Check CRC32 */
    if (custom_fs_compute_and_check_external_bundle_crc32() == RETURN_NOK)
    {
        /* Wrong CRC32 : invalid bundle, start application which will also detect it */
        custom_fs_settings_clear_fw_upgrade_flag();
        start_application();
    }

    /* Setup DMA controller for data flash transfers */
    dma_init();
    
    #if defined(PLAT_V7_SETUP)
    /* Initialize OLED ports & power ports, used for OLED PSU */
    platform_io_init_power_ports();
    platform_io_init_oled_ports();
    
    /* Initialize the OLED only if 3V3 is present */
    BOOL is_usb_power_present_at_boot = platform_io_is_usb_3v3_present_raw();
    if (is_usb_power_present_at_boot != FALSE)
    {
        platform_io_power_up_oled(TRUE);
        sh1122_init_display(&plat_oled_descriptor, FALSE);
    }
    #endif

    /* Fetch encryption & signing keys & IVs: TODO */
    #if defined(PLAT_V7_SETUP)
    memset(encryption_aes_key, 0, sizeof(encryption_aes_key));
    memset(encryption_aes_iv, 0, sizeof(encryption_aes_iv));
    memset(signing_aes_key, 0, sizeof(signing_aes_key));
    #endif
    
    /* Automatic flash write, disable caching */
    NVMCTRL->CTRLB.bit.MANW = 0;
    NVMCTRL->CTRLB.bit.CACHEDIS = 1;

    /* Two passes: one to check the signature, one to flash and check previously stored signature for fw file */
    for (uint16_t nb_pass = 0; nb_pass < 2; nb_pass++)
    {
        #if defined(PLAT_V7_SETUP)
        /* Initialize encryption context, set IV to 0 */
        br_aes_ct_ctrcbc_init(&bootloader_signing_aes_context, signing_aes_key, AES_KEY_LENGTH/8);
        memset((void*)cur_cbc_mac, 0x00, sizeof(cur_cbc_mac));
        #endif
        
        #if defined(PLAT_V7_SETUP)
        /* Screen debug */
        if ((is_usb_power_present_at_boot != FALSE) && (platform_io_is_usb_3v3_present_raw() != FALSE))
        {
            if (nb_pass == 0)
                sh1122_erase_screen_and_put_top_left_emergency_string(&plat_oled_descriptor, u"Checking...");
            else
                sh1122_erase_screen_and_put_top_left_emergency_string(&plat_oled_descriptor, u"Flashing...");
        }
        #endif

        /* Set current dataflash address at the beginning of the signed data */
        current_data_flash_addr = custom_fs_get_start_address_of_signed_data();

        /* Booleans to know if we are in the right address space to fetch firmware data */
        uint32_t address_in_mcu_memory = APP_START_ADDR;
        BOOL address_passed_for_fw_data = FALSE;
        BOOL address_valid_for_fw_data = FALSE;

        /* Arm first DMA transfer */
        custom_fs_continuous_read_from_flash(bundle_data_b1.uint8_row, current_data_flash_addr, sizeof(bundle_data_b1), TRUE);
        available_data_buffer = bundle_data_b2.uint8_row;
        received_data_buffer = bundle_data_b1.uint8_row;

        /* CBCMAC the complete memory */
        while (current_data_flash_addr < W25Q16_FLASH_SIZE)
        {
            /* Compute number of bytes to read */
            uint32_t nb_bytes_to_read = W25Q16_FLASH_SIZE - current_data_flash_addr;
            if (nb_bytes_to_read > sizeof(bundle_data_b1))
            {
                nb_bytes_to_read = sizeof(bundle_data_b1);
            }

            /* Wait for received DMA transfer */
            while(dma_custom_fs_check_and_clear_dma_transfer_flag() == FALSE);

            /* Arm next DMA transfer */
            if (available_data_buffer == bundle_data_b1.uint8_row)
            {
                custom_fs_get_other_data_from_continuous_read_from_flash(bundle_data_b1.uint8_row, sizeof(bundle_data_b1), TRUE);
            } 
            else
            {
                custom_fs_get_other_data_from_continuous_read_from_flash(bundle_data_b2.uint8_row, sizeof(bundle_data_b2), TRUE);
            }

            /* CBCMAC the crap out of it */
            #if defined(PLAT_V7_SETUP)
            br_aes_ct_ctrcbc_mac(&bootloader_signing_aes_context, cur_cbc_mac, received_data_buffer, nb_bytes_to_read);
            #endif

            /* Where the fw data is valid inside our read buffer */
            uint32_t valid_fw_data_offset = 0;

            /* Check if we are in the right address space for fw data */
            if ((address_valid_for_fw_data == FALSE) && (address_passed_for_fw_data == FALSE) && ((current_data_flash_addr + nb_bytes_to_read) > fw_file_address))
            {
                /* Our firmware is longer than a page, we should be good ;) */
                address_valid_for_fw_data = TRUE;

                /* Compute the fw data offset */
                valid_fw_data_offset = fw_file_address - current_data_flash_addr;
            }
            
            /* Do we need to flash stuff? */
            if ((nb_pass == 1) && (address_valid_for_fw_data != FALSE))
            {
                /* Number of valid fw bytes in the current buffer. Flashing more data than required is fine... we'll pad inside the bundle anyway */
                uint16_t nb_valid_fw_bytes_to_copy = sizeof(bundle_data_b1) - valid_fw_data_offset;
                
                /* Check for overflow, then in that case update the spillover variable and the number of bytes to store */
                uint16_t nb_bytes_over = 0;
                if (nb_valid_fw_bytes_to_copy + nb_fw_bytes_in_row_buffer > sizeof(row_to_be_flashed))
                {
                    nb_bytes_over = nb_valid_fw_bytes_to_copy + nb_fw_bytes_in_row_buffer - sizeof(row_to_be_flashed);
                    nb_valid_fw_bytes_to_copy -= nb_bytes_over;                    
                }
                
                /* Actually copy the bytes... */
                memcpy(&row_to_be_flashed.uint8_row[nb_fw_bytes_in_row_buffer], &received_data_buffer[valid_fw_data_offset], nb_valid_fw_bytes_to_copy);
                
                /* Update the number of fw_bytes now in the buffer */
                nb_fw_bytes_in_row_buffer += nb_valid_fw_bytes_to_copy;
                
                /* Flashing required?, note that sizeof(row_to_be_flashed) == NVMCTRL_ROW_SIZE */
                if (nb_fw_bytes_in_row_buffer == NVMCTRL_ROW_SIZE)
                {
                    /* Flash a full memory row */
                    store_row_buffer_into_main_memory(address_in_mcu_memory, row_to_be_flashed.uint16_row);
                    
                    /* Increment address counter */
                    address_in_mcu_memory += NVMCTRL_ROW_SIZE;
                            
                    /* Row buffer now empty */
                    nb_fw_bytes_in_row_buffer = 0;
                }
                
                /* Check for spillover */
                if (nb_bytes_over != 0)
                {
                    memcpy(&row_to_be_flashed.uint8_row[nb_fw_bytes_in_row_buffer], &received_data_buffer[valid_fw_data_offset + nb_valid_fw_bytes_to_copy], nb_bytes_over);
                    nb_fw_bytes_in_row_buffer += nb_bytes_over;
                }
            }
            
            /* Check if we just passed the end of fw data */
            if ((address_valid_for_fw_data != FALSE) && ((current_data_flash_addr + nb_bytes_to_read) > (fw_file_address + fw_file_size)))
            {
                address_passed_for_fw_data = TRUE;
                address_valid_for_fw_data = FALSE;
                
                /* First pass: store cbcmac at end of fw file (kind of), second pass: flash & check cbcmac */
                if (nb_pass == 0)
                {
                    #if defined(PLAT_V7_SETUP)
                    memcpy(cbc_mac_to_end_of_mcu_fpass, cur_cbc_mac, sizeof(cbc_mac_to_end_of_mcu_fpass));
                    #endif
                }
                
                /* Second pass: check the previously stored CBCMAC to make sure data wasn't altered in between (which is why we used CBC) */
                if (nb_pass == 1)
                {
                    /* Flash the remaining data if needed */
                    if (nb_fw_bytes_in_row_buffer != 0)
                    {
                        store_row_buffer_into_main_memory(address_in_mcu_memory, row_to_be_flashed.uint16_row);
                    }
                    
                    #if defined(PLAT_V7_SETUP)
                    if (utils_side_channel_safe_memcmp(cbc_mac_to_end_of_mcu_fpass, cur_cbc_mac, sizeof(cbc_mac_to_end_of_mcu_fpass)) != 0)
                    {
                        /* Somehow the CBCMAC changed between both passes, which can only be explained by a malicious attempt */
                        brick_main_mcu();
                        platform_io_disable_switch_and_die();
                        while(1);
                    }
                    #endif

                    /* Do not perform CBC mac until end of bundle */
                    nb_pass = 33;
                    break;                    
                }            
            }

            /* Increment scan address */
            current_data_flash_addr += nb_bytes_to_read;

            /* Set correct buffer pointers, DMA transfers were already triggered */
            if (available_data_buffer == bundle_data_b1.uint8_row)
            {
                available_data_buffer = bundle_data_b2.uint8_row;
                received_data_buffer = bundle_data_b1.uint8_row;
            }
            else
            {
                available_data_buffer = bundle_data_b1.uint8_row;
                received_data_buffer = bundle_data_b2.uint8_row;
            }
        }

        /* End of pass */
        while(dma_custom_fs_check_and_clear_dma_transfer_flag() == FALSE);
        custom_fs_stop_continuous_read_from_flash();
    }
    
    #if defined(PLAT_V7_SETUP)    
    /* Switch off OLED */
    if (is_usb_power_present_at_boot != FALSE)
    {
        sh1122_oled_off(&plat_oled_descriptor);
        platform_io_power_down_oled();
        
    }
    #endif
    
    /* Final wait, clear flag, reset */
    while ((NVMCTRL->INTFLAG.reg & NVMCTRL_INTFLAG_READY) == 0);
    custom_fs_settings_clear_fw_upgrade_flag();
    NVIC_SystemReset();
    while(1);
}
