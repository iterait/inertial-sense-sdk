/**
 * @file ISBootloaderThread.cpp
 * @author Dave Cutting (davidcutting42@gmail.com)
 * @brief Inertial Sense routines for updating embedded systems in parallel
 * 
 */

/*
MIT LICENSE

Copyright (c) 2014-2022 Inertial Sense, Inc. - http://inertialsense.com

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files(the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "ISBootloaderThread.h"
#include "ISBootloaderDFU.h"
#include "ISBootloaderISB.h"
#include "ISBootloaderSAMBA.h"
#include "ISSerialPort.h"

vector<is_device_context*> ISBootloader::ctx;
void (*ISBootloader::waitAction)();
static bool any_in_progress(vector<is_device_context*>& ctx);

is_operation_result ISBootloader::init(
    vector<string>&             comPorts,
    int                         baudRate,
    const char*                 firmware,
    pfnBootloadProgress         uploadProgress, 
    pfnBootloadProgress         verifyProgress, 
    pfnBootloadStatus           infoProgress,
    void*                       user_data,
    void						(*waitActionPtr)()
)
{
    waitAction = waitActionPtr;

    is_dfu_list dfu_list;
    size_t i, j;

    for(i = 0; i < ctx.size(); i++)
    {
        is_destroy_context(ctx[i]);
    }
    ctx.clear();

    for(i = 0; i < comPorts.size(); i++)
    {	// Create contexts for devices in serial mode (APP/ISB/SAMBA)
        is_device_handle handle;
        memset(&handle, 0, sizeof(is_device_handle));
        strncpy(handle.port_name, comPorts[i].c_str(), 100);
        handle.status = IS_HANDLE_TYPE_SERIAL;
        handle.baud = baudRate;
        ctx.push_back(is_create_context(
            &handle,
            firmware, 
            uploadProgress, 
            verifyProgress, 
            infoProgress,
            user_data
        ));
    }

    change_mode_devices(true);

    // re-discover devices
    
    // Discover all serial ports
    vector<string> ports;
    vector<is_device_context*> ctx_tmp;
    vector<is_device_context*> ctx_new;
	cISSerialPort::GetComPorts(ports);

    for(int i = 0; i < ports.size(); i++)
    {
        is_device_handle handle;
        memset(&handle, 0, sizeof(is_device_handle));
        strncpy(handle.port_name, comPorts[i].c_str(), 100);
        handle.status = IS_HANDLE_TYPE_SERIAL;
        handle.baud = baudRate;
        ctx_tmp.push_back(is_create_context(
            &handle,
            firmware, 
            uploadProgress, 
            verifyProgress, 
            infoProgress,
            user_data
        ));
    }

    // Probe the serial ports, false so we don't change mode yet.
    change_mode_devices(false);

    for(int i = 0; i < ctx.size(); i++)
    {
        for(int j = 0; j < ctx_tmp.size(); j++)
        {
            if(ctx[i]->props.serial == ctx_tmp[j]->props.serial && ctx[i]->props.serial > 1 && ctx[i]->props.serial < 99999)
            {   // If they match serials, push back 
                ctx_new.push_back(ctx_tmp[j]);
                ctx_tmp.erase(ctx_tmp.begin() + j);
            }
        }  
    }

    // Replace the old list
    for(i = 0; i < ctx_tmp.size(); i++)
    {
        is_destroy_context(ctx_tmp[i]);
    }
    for(i = 0; i < ctx.size(); i++)
    {
        is_destroy_context(ctx[i]);
    }
    ctx.clear();
    ctx_tmp.clear();
    ports.clear();
    for(i = 0; i < ctx_new.size(); i++)
    {
        ctx.push_back(ctx_new[i]);
    }

    // Change mode if needed for a second time
    change_mode_devices(true);

    // Discover all serial ports a second time
	cISSerialPort::GetComPorts(ports);

    for(int i = 0; i < ports.size(); i++)
    {
        is_device_handle handle;
        memset(&handle, 0, sizeof(is_device_handle));
        strncpy(handle.port_name, comPorts[i].c_str(), 100);
        handle.status = IS_HANDLE_TYPE_SERIAL;
        handle.baud = baudRate;
        ctx_tmp.push_back(is_create_context(
            &handle,
            firmware, 
            uploadProgress, 
            verifyProgress, 
            infoProgress,
            user_data
        ));
    }

    // Probe the serial ports, false so we don't change mode yet.
    change_mode_devices(false);

    for(int i = 0; i < ctx.size(); i++)
    {
        for(int j = 0; j < ctx_tmp.size(); j++)
        {
            if(ctx[i]->props.serial == ctx_tmp[j]->props.serial && ctx[i]->props.serial > 1 && ctx[i]->props.serial < 99999)
            {   // If they match serials, push back 
                ctx_new.push_back(ctx_tmp[j]);
                ctx_tmp.erase(ctx_tmp.begin() + j);
            }
        }  
    }

    // Replace the old list
    for(i = 0; i < ctx_tmp.size(); i++)
    {
        is_destroy_context(ctx_tmp[i]);
    }
    for(i = 0; i < ctx.size(); i++)
    {
        is_destroy_context(ctx[i]);
    }
    ctx.clear();
    ctx_tmp.clear();
    ports.clear();
    for(i = 0; i < ctx_new.size(); i++)
    {
        ctx.push_back(ctx_new[i]);
    }

    // Add DFU devices to list

    if(libusb_init(NULL) < 0) return IS_OP_ERROR;
    is_dfu_list_devices(&dfu_list);

    for(i = 0; i < dfu_list.present; i++)
    {	// Create contexts for devices in DFU mode
        is_device_handle handle;
        memset(&handle, 0, sizeof(is_device_handle));
        handle.status = IS_HANDLE_TYPE_LIBUSB;
        memcpy(&handle.dfu.uid, &dfu_list.id[i].uid, IS_DFU_UID_MAX_SIZE);
        handle.dfu.vid = dfu_list.id[i].vid;
        handle.dfu.pid = dfu_list.id[i].pid;
        handle.dfu.sn = 0;
        ctx.push_back(is_create_context(
            &handle,
            firmware, 
            uploadProgress, 
            verifyProgress, 
            infoProgress,
            user_data
        ));
    }
}

is_operation_result ISBootloader::change_mode_devices(bool enter)
{
    for(int i = 0; i < ctx.size(); i++)
    {	// Start threads
        ctx[i]->enter = enter;
        ctx[i]->update_in_progress = true;
        ctx[i]->thread = threadCreateAndStart(mode_thread, (void*)ctx[i]);
    }

    while (1)
    {	// Wait for threads to finish
        if(ctx.size()) for(int i = 0; i < ctx.size(); i++)
        {
            if((ctx[i]->thread != NULL) && !ctx[i]->update_in_progress)
            {
                threadJoinAndFree(ctx[i]->thread);
                ctx[i]->thread = NULL;
            }

            if(!any_in_progress(ctx))
            {
                libusb_exit(NULL);
                return IS_OP_OK;
            }
        }
        else return IS_OP_OK;

        if(waitAction != 0) waitAction();

        SLEEP_MS(10);
    }

    return IS_OP_OK;
}

// size_t ISBootloader::get_num_devices(vector<string>& comPorts)
// {
//     size_t ret = 0;

//     is_dfu_list dfu_list;
//     is_dfu_list_devices(&dfu_list);

//     ret += dfu_list.present;
//     ret += comPorts.size();

//     return ret;
// }

static bool any_in_progress(vector<is_device_context*>& ctx)
{
    for(size_t i = 0; i < ctx.size(); i++)
    {
        if(ctx[i]->thread != NULL)
        {
            return true;
        }
    }

    return false;
}

is_operation_result ISBootloader::update()
{
    for(int i = 0; i < ctx.size(); i++)
    {	// Start threads
        ctx[i]->thread = threadCreateAndStart(update_thread, (void*)ctx[i]);
    }

    while (1)
    {	// Wait for threads to finish
        if(ctx.size()) for(int i = 0; i < ctx.size(); i++)
        {
            if((ctx[i]->thread != NULL) && !ctx[i]->update_in_progress)
            {
                threadJoinAndFree(ctx[i]->thread);
                ctx[i]->thread = NULL;
            }

            if(!any_in_progress(ctx))
            {
                libusb_exit(NULL);
                return IS_OP_OK;
            }
        }
        else return IS_OP_OK;

        if(waitAction != 0) waitAction();

        SLEEP_MS(10);
    }
    
    libusb_exit(NULL);
    return IS_OP_OK;
}

void ISBootloader::update_thread(void* context)
{
    is_update_flash(context);
}

void ISBootloader::mode_thread(void* context)
{
    is_device_context* ctx = (is_device_context*)context;

    const char * extension = get_file_ext(ctx->firmware_path);
    uint32_t file_signature = 0;
    uint32_t valid_signatures = 0;
    bool old_bootloader_version = false;
    is_bootloader_state state = IS_BOOTLOADER_STATE_NONE;
    is_bootloader_state required_state = IS_BOOTLOADER_STATE_NONE;

    if(strcmp(extension, "bin") == 0)
    {
        file_signature = is_get_bin_image_signature(ctx);
    }
    else if(strcmp(extension, "hex") == 0)
    {
        file_signature = is_get_hex_image_signature(ctx);
    }
    else
    {
        ctx->info_callback(ctx, "Invalid firmware filename extension", IS_LOG_LEVEL_ERROR);
        ctx->update_in_progress = false;
        return;
    }

    if(file_signature & (IS_IMAGE_SIGN_UINS_3_16K | IS_IMAGE_SIGN_UINS_3_24K |
                        IS_IMAGE_SIGN_EVB_2_16K | IS_IMAGE_SIGN_EVB_2_24K |
                        IS_IMAGE_SIGN_UINS_5))
    {
        required_state = IS_BOOTLOADER_STATE_ISB;   
    }
    else if(file_signature & (IS_IMAGE_SIGN_ISB_SAMx70_16K | IS_IMAGE_SIGN_ISB_SAMx70_24K | IS_IMAGE_SIGN_ISB_STM32L4))
    {
        required_state = IS_BOOTLOADER_STATE_ROM;
    }
    else
    {   
        ctx->update_in_progress = false;
        return;
    }

    if(ctx->handle.status == IS_HANDLE_TYPE_LIBUSB)
    {   /** DFU MODE - can't change mode except by updating ISB */
        valid_signatures |= IS_IMAGE_SIGN_ISB_STM32L4;

        state = IS_BOOTLOADER_STATE_ROM;
        ctx->update_in_progress = false;
        return;
    }
    else if(ctx->handle.status == IS_HANDLE_TYPE_SERIAL)
    {
        if (is_app_get_version(ctx) == IS_OP_OK)
        {   /** APP MODE */
            /* In App mode, we don't have a way to know whether we have a 16K or
            * 24K bootloader. Accept all uINS and EVB images that match. Call
            * this function again when you are in ISB mode.
            */
            if (ctx->props.app.uins_version[0] == 5)
            {   /** uINS-5 */
                valid_signatures |= IS_IMAGE_SIGN_UINS_5;
                valid_signatures |= IS_IMAGE_SIGN_ISB_STM32L4;
            }
            else if (ctx->props.app.uins_version[0] == 3 || ctx->props.app.uins_version[0] == 4)
            {   /** uINS-3/4 */
                valid_signatures |= IS_IMAGE_SIGN_UINS_3_16K | IS_IMAGE_SIGN_UINS_3_24K;
                valid_signatures |= IS_IMAGE_SIGN_ISB_SAMx70_16K | IS_IMAGE_SIGN_ISB_SAMx70_24K;
            }

            if (ctx->props.app.evb_version[0] == 2)
            {   /** EVB-2 */
                valid_signatures |= IS_IMAGE_SIGN_EVB_2_16K | IS_IMAGE_SIGN_EVB_2_24K;
                valid_signatures |= IS_IMAGE_SIGN_ISB_SAMx70_16K | IS_IMAGE_SIGN_ISB_SAMx70_24K;
            }

            state = IS_BOOTLOADER_STATE_APP;
        }
        else if(is_isb_handshake(ctx) == IS_OP_OK && is_isb_negotiate_version(ctx) == IS_OP_OK && is_isb_get_version(ctx) == IS_OP_OK)
        {   /** IS BOOTLOADER MODE */
            if(ctx->props.isb.major >= 6)   
            {   // v6 and has EVB detection built-in
                if(ctx->props.isb.processor == IS_PROCESSOR_SAMx70)
                {   
                    valid_signatures |= ctx->props.isb.is_evb ? IS_IMAGE_SIGN_EVB_2_24K : IS_IMAGE_SIGN_UINS_3_24K;
                    valid_signatures |= IS_IMAGE_SIGN_ISB_SAMx70_16K | IS_IMAGE_SIGN_ISB_SAMx70_24K;
                }
                else if(ctx->props.isb.processor == IS_PROCESSOR_STM32L4)
                {
                    valid_signatures |= IS_IMAGE_SIGN_UINS_5;
                    valid_signatures |= IS_IMAGE_SIGN_ISB_STM32L4;
                }
            }
            else
            {
                valid_signatures |= IS_IMAGE_SIGN_EVB_2_16K | IS_IMAGE_SIGN_UINS_3_16K;
                valid_signatures |= IS_IMAGE_SIGN_ISB_SAMx70_16K | IS_IMAGE_SIGN_ISB_SAMx70_24K;
                old_bootloader_version = true;
            }

            state = IS_BOOTLOADER_STATE_ISB;
        }
        else if(is_samba_init(ctx) == IS_OP_OK)
        {   /** SAM-BA MODE */
            valid_signatures |= IS_IMAGE_SIGN_ISB_SAMx70_16K | IS_IMAGE_SIGN_ISB_SAMx70_24K;

            state = IS_BOOTLOADER_STATE_ROM;

            is_samba_get_serial(ctx);
        }
        else
        {
            state = IS_BOOTLOADER_STATE_NONE;
            ctx->update_in_progress = false;
            return;
        }
    }
    else
    {   // Invalid handle status
        state = IS_BOOTLOADER_STATE_NONE;
        ctx->update_in_progress = false;
        return;
    }

    // Compare the bitfields
    if (file_signature & valid_signatures)
    {
        if(required_state > state && ctx->enter)
        {
            // Jump down one mode (APP -> ISB, ISB -> ROM)
            if(state == IS_BOOTLOADER_STATE_APP)
            {
                if(file_signature & (IS_IMAGE_SIGN_EVB_2_16K | IS_IMAGE_SIGN_EVB_2_24K)) is_isb_enable(ctx, "EBLE");
                else is_isb_enable(ctx, "BLEN");
            }
            else if(state == IS_BOOTLOADER_STATE_ISB)
            {
                is_isb_restart_rom(&ctx->handle.port);
            }
        }
        else
        {
            // Can't do anything other than update to get to next level up
        }
    }

    ctx->update_in_progress = false;
}