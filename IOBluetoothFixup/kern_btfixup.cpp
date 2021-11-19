/*
 *  Released under "The GNU General Public License (GPL-2.0)"
 *
 *  Copyright (c) 2021 cjiang. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include "kern_btfixup.hpp"
#include <IOKit/bluetooth/transport/IOBluetoothHostControllerTransport.h>

static IOBtFixup * callback = nullptr;

void IOBtFixup::init()
{
    callback = this;
    
    lilu.onKextLoadForce(kextList, arrsize(kextList),
    [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
        callback->processKext(patcher, index, address, size);
    }, this);
}

void IOBtFixup::deinit()
{
    
}

void IOBtFixup::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size)
{
    if ( index == kextList[0].loadIndex )
        orgIntelBluetoothHostController_metaClass_alloc = patcher.solveSymbol(index, itlBtMetaClassAllocSymbol);
    else if ( index == kextList[1].loadIndex )
    {
        orgIOBluetoothHostController_metaClass_alloc = patcher.solveSymbol(index, ioBtMetaClassAllocSymbol);
        orgBrcmBluetoothHostController_metaClass_alloc = patcher.solveSymbol(index, brcmBtMetaClassAllocSymbol);
        orgABrcmBluetoothHostController_metaClass_alloc = patcher.solveSymbol(index, aBrcmBtMetaClassAllocSymbol);
        orgCSRBluetoothHostController_metaClass_alloc = patcher.solveSymbol(index, csrBtMetaClassAllocSymbol);
        orgACSRBluetoothHostController_metaClass_alloc = patcher.solveSymbol(index, aCsrBtMetaClassAllocSymbol);

		KernelPatcher::RouteRequest request1[]
		{
			KernelPatcher::RouteRequest(createBluetoothHostControllerObjectSymbol, CreateBluetoothHostControllerObject, orgIOBluetoothFamily_CreateBluetoothHostControllerObject)
		};
        KernelPatcher::RouteRequest request2[]
        {
			KernelPatcher::RouteRequest(needToWaitForControllerToShowUpSymbol, NeedToWaitForControllerToShowUp, orgIOBluetoothFamily_NeedToWaitForControllerToShowUp)
        };

		if ( !patcher.routeMultiple(index, request1, arrsize(request1), address, size) )
        {
OVER:
            SYSLOG("IOBtFixup", "patcher.routeMultiple failed with error %d", patcher.getError());
            patcher.clearError();
			return;
        }

		if ( getKernelVersion() >= KernelVersion::BigSur && !patcher.routeMultiple(index, request2, arrsize(request2), address, size) )
			goto OVER;
    }
}

IOReturn IOBtFixup::CreateBluetoothHostControllerObject(IOBluetoothHCIController * that, BluetoothHardwareListType * hardware)
{
    IOBluetoothHostController * controller;

    if ( !hardware || !hardware->mBluetoothTransport )
        return kIOReturnError;
  
    switch ( *(UInt16 *) (hardware->mBluetoothTransport + 176) ) //mControllerVendorType
    {
        case 2:
        case 6:
        case 7:
            controller = (AppleBroadcomBluetoothHostController *) ((metaClassAlloc) callback->orgABrcmBluetoothHostController_metaClass_alloc)();
            break;
        case 3:
            controller = (AppleCSRBluetoothHostController *) ((metaClassAlloc) callback->orgACSRBluetoothHostController_metaClass_alloc)();
            break;
        case 4:
            controller = (BroadcomBluetoothHostController *) ((metaClassAlloc) callback->orgBrcmBluetoothHostController_metaClass_alloc)();
            break;
        case 5:
            controller = (CSRBluetoothHostController *) ((metaClassAlloc) callback->orgCSRBluetoothHostController_metaClass_alloc)();
            break;
        case 8:
            controller = (IntelBluetoothHostController *) ((metaClassAlloc) callback->orgIntelBluetoothHostController_metaClass_alloc)();
            break;
        default:
            controller = (IOBluetoothHostController *) ((metaClassAlloc) callback->orgIOBluetoothHostController_metaClass_alloc)();
            break;
    }
    
    if ( !controller )
        return kIOReturnError;
    
    if ( controller->init(that, hardware->mBluetoothTransport) && controller->attach(that) )
    {
        *(IOBluetoothHostControllerTransport **) (controller + 832) = hardware->mBluetoothTransport;
        if ( controller->start(that) )
        {
            *(IOBluetoothHostController **) (hardware->mBluetoothTransport + 144) = controller;
            hardware->mBluetoothHostController = controller;
            return 0;
        }
        controller->detach(that);
    }
    
    OSSafeReleaseNULL(controller);
    return kIOReturnError;
}

bool IOBtFixup::NeedToWaitForControllerToShowUp(IOBluetoothHCIController * that)
{
    return true;
}
