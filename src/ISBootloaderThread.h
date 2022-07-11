/**
 * @file ISBootloaderThread.h
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

#ifndef __IS_BOOTLOADER_H_
#define __IS_BOOTLOADER_H_

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "ISUtilities.h"
#include "ISBootloader.h"

using namespace std; 

class ISBootloader
{
public:
    ISBootloader() {};
    ~ISBootloader() {};

    /**
     * @brief Specify which devices to update, and other parameters. Adds 
     *  devices to a list of known devices based on their serial numbers. This 
     *  function creates the device contexts used later on.
     * @note Call once at start.
     * 
     * @param comPorts list of serial port names, i.e. `COM1` or `/dev/ttyACM0`.
     * @param baudRate 
     * @param firmware full path to firmware image. Must be .hex, unless you are
     *  updating uINS-3/EVB-2 bootloader, in which case it must be .bin.
     * @param uploadProgress callback to get percentage.
     * @param verifyProgress callback to get percentage.
     * @param infoProgress Callback to get messages about progress.
     * @param user_data Unused.
     * @param waitAction Callback to update UI and do other things while waiting for update.
     * @return is_operation_result 
     */
    static is_operation_result init(
        vector<string>&             comPorts,
        int                         baudRate,
        const char*                 firmware,
        pfnBootloadProgress         uploadProgress, 
        pfnBootloadProgress         verifyProgress, 
        pfnBootloadStatus           infoProgress,
        void*                       user_data,
        void						(*waitAction)()
    );
    
    

    static is_operation_result update();

    static vector<is_device_context*> ctx;

private:
    static void update_thread(void* context);
    static void mode_thread(void* context);
    static void (*waitAction)();
    
    /**
     * @brief Changes the mode of devices in the list of devices. This function 
     *  also follows up to see where the devices went, as their port numbers
     *  may change. Call twice if you are updating the ISB bootloader to ensure
     *  devices get into ROM mode. Changes mode to whatever is needed to bootload
     *  the firmware file specified in `init()`
     * 
     * @param all_connected if true, all connected devices will be affected 
     *  instead of just those specified. All DFU devices are affected regardless 
     *  of this option.
     * @return is_operation_result 
     */
    static is_operation_result change_mode_devices(bool all_connected = false);
};

#endif // __IS_BOOTLOADER_H_
