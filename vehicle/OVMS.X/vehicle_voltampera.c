/*
;    Project:       Open Vehicle Monitor System
;    Date:          6 May 2012
;
;    Changes:
;    1.0  Initial release
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
; THE SOFTWARE.
*/

#include <stdlib.h>
#include <delays.h>
#include <string.h>
#include "ovms.h"
#include "params.h"

// Volt/Ampera state variables

#pragma udata overlay vehicle_overlay_data
unsigned int soc_largest  = 56028;
unsigned int soc_smallest = 13524;
BOOL bus_is_active = FALSE;       // Indicates recent activity on the bus

#pragma udata

////////////////////////////////////////////////////////////////////////
// vehicle_voltampera_polls
// This rom table records the extended PIDs that need to be polled

rom struct
  {
  unsigned int moduleid;
  unsigned char polltime;
  unsigned int pid;
  } vehicle_voltampera_polls[]
  =
  {
    { 0x07E0, 10, 0x0046 },
    { 0x07E4, 10, 0x4369 },
    { 0x07E4, 10, 0x4368 },
    { 0x07E4, 10, 0x801f },
    { 0x07E4, 10, 0x801e },
    { 0x07E4, 10, 0x434f },
    { 0x07E4, 10, 0x1c43 },
    { 0x0000, 0,  0x0000 }
  };

////////////////////////////////////////////////////////////////////////
// vehicle_voltampera_ticker1()
// This function is an entry point from the main() program loop, and
// gives the CAN framework a ticker call approximately once per second
//
BOOL vehicle_voltampera_ticker1(void)
  {
  int k;
  BOOL doneone = FALSE;

  // bus_is_active indicates we've recently seen a message on the can bus
  // Quick exit if bus is recently not active
  if (!bus_is_active) return FALSE;

  // Also, we need CAN_WRITE enabled, so return if not
  if (sys_features[FEATURE_CANWRITE]==0) return FALSE;

  // Let's run through and see if we have to poll for any data..
  for (k=0;vehicle_voltampera_polls[k].moduleid != 0; k++)
    {
    if ((can_granular_tick % vehicle_voltampera_polls[k].polltime) == 0)
      {
      // OK. Let's send it...
      if (doneone)
        delay5b(); // Delay a little... (5ms, approx)

      while (TXB0CONbits.TXREQ) {} // Loop until TX is done
      TXB0CON = 0;
      TXB0SIDL = (vehicle_voltampera_polls[k].moduleid & 0x07)<<5;
      TXB0SIDH = (vehicle_voltampera_polls[k].moduleid>>3);
      TXB0D0 = 0x03;
      TXB0D1 = 0x22;        // Get extended PID
      TXB0D2 = vehicle_voltampera_polls[k].pid >> 8;
      TXB0D3 = vehicle_voltampera_polls[k].pid & 0xff;
      TXB0D4 = 0x00;
      TXB0D5 = 0x00;
      TXB0D6 = 0x00;
      TXB0D7 = 0x00;
      TXB0DLC = 0b00001000; // data length (8)
      TXB0CON = 0b00001000; // mark for transmission
      doneone = TRUE;
      }
    }
  // Assume the bus is not active, so we won't poll any more until we see
  // activity on the bus
  bus_is_active = FALSE;
  return FALSE;
  }

////////////////////////////////////////////////////////////////////////
// can_poll()
// This function is an entry point from the main() program loop, and
// gives the CAN framework an opportunity to poll for data.
//
BOOL vehicle_voltampera_poll0(void)
  {
  unsigned int pid;
  unsigned char value;

  can_datalength = RXB0DLC & 0x0F; // number of received bytes
  can_databuffer[0] = RXB0D0;
  can_databuffer[1] = RXB0D1;
  can_databuffer[2] = RXB0D2;
  can_databuffer[3] = RXB0D3;
  can_databuffer[4] = RXB0D4;
  can_databuffer[5] = RXB0D5;
  can_databuffer[6] = RXB0D6;
  can_databuffer[7] = RXB0D7;

  RXB0CONbits.RXFUL = 0; // All bytes read, Clear flag

  if (can_databuffer[1] != 0x62) return TRUE; // Check the return code

  pid = (unsigned int)can_databuffer[2]<<8 + can_databuffer[3];
  value = can_databuffer[4];
  
  switch (pid)
    {
    case 0x4369:  // On-board charger current
      car_chargecurrent = (unsigned int)value / 5;
      break;
    case 0x4368:  // On-board charger voltage
      car_linevoltage = (unsigned int)value << 1;
      break;
    case 0x801f:  // Outside temperature (filtered)
      car_ambient_temp = ((int)value >> 1) - 0x28;
      break;
    case 0x801e:  // Outside temperature (raw)
      break;
    case 0x434f:  // High-voltage Battery temperature
      car_tbattery = (int)value - 0x28;
      break;
    case 0x1c43:  // PEM temperature
      car_tpem = (int)value - 0x28;
      break;
    case 0x0046:  // Ambient temperature
      car_ambient_temp = (signed char)((int)value - 0x28);
      break;
    }

  return TRUE;
  }

BOOL vehicle_voltampera_poll1(void)
  {
  unsigned char CANctrl;
  unsigned char k;

  can_datalength = RXB1DLC & 0x0F; // number of received bytes
  can_databuffer[0] = RXB1D0;
  can_databuffer[1] = RXB1D1;
  can_databuffer[2] = RXB1D2;
  can_databuffer[3] = RXB1D3;
  can_databuffer[4] = RXB1D4;
  can_databuffer[5] = RXB1D5;
  can_databuffer[6] = RXB1D6;
  can_databuffer[7] = RXB1D7;

  CANctrl=RXB1CON;		// copy CAN RX1 Control register
  RXB1CONbits.RXFUL = 0; // All bytes read, Clear flag

  bus_is_active = TRUE; // Activity has been seen on the bus

  if ((CANctrl & 0x07) == 2)             // Acceptance Filter 2 (RXF2) = CAN ID 0x206
    {
    // SOC
    // For the SOC, each 4,000 is 1kWh. Assuming a 16.1kWh battery, 1% SOC is 644 decimal bytes
    // The SOC itself is d1<<8 + d2
    unsigned int v = (can_databuffer[1]+((unsigned int) can_databuffer[0] << 8));
    if ((v<soc_smallest)&&(v>0)) v=soc_smallest;
    if (v>soc_largest) v=soc_largest;
    car_SOC = (char)((v-soc_smallest)/((soc_largest-soc_smallest)/100));
    }
  else if ((CANctrl & 0x07) == 3)        // Acceptance Filter 3 (RXF3) = CAN ID 4E1
    {
    // The VIN can be constructed by taking the number "1" and converting the CAN IDs 4E1 and 514 to ASCII.
    // So with "4E1 4255313032363839" and "514 4731524436453436",
    // you would end up with 42 55 31 30 32 36 38 39 47 31 52 44 36 45 34 36,
    // where 42 is ASCII for B, 55 is U, etc.
    for (k=0;k<8;k++)
      car_vin[k+9] = can_databuffer[k];
    car_vin[17] = 0;
    }
  else if ((CANctrl & 0x07) == 4)        // Acceptance Filter 4 (RXF4) = CAN ID 514
    {
    car_vin[0] = '1';
    for (k=0;k<8;k++)
      car_vin[k+1] = can_databuffer[k];
    }
  return TRUE;
  }

////////////////////////////////////////////////////////////////////////
// vehicle_voltampera_initialise()
// This function is an entry point from the main() program loop, and
// gives the CAN framework an opportunity to initialise itself.
//
BOOL vehicle_voltampera_initialise(void)
  {
  char *p;

  car_type[0] = 'V'; // Car is type VA - Volt/Ampera
  car_type[1] = 'A';
  car_type[2] = 0;
  car_type[3] = 0;
  car_type[4] = 0;

  CANCON = 0b10010000; // Initialize CAN
  while (!CANSTATbits.OPMODE2); // Wait for Configuration mode

  // We are now in Configuration Mode
  RXB0CON = 0b00000000; // RX buffer0 uses Mask RXM0 and filters RXF0, RXF1

  RXM0SIDL = 0b10100000;
  RXM0SIDH = 0b11111111;	// Set Mask0 to 0x7fd

  // Filter 0: Used for ID#0x7E8
  RXF0SIDL = 0b00000000;
  RXF0SIDH = 0b11111101;

  // Filter 1: Used for ID#0x7EC
  RXF1SIDL = 0b10000000;
  RXF1SIDH = 0b11111101;

  RXB1CON  = 0b00000000;	// RX buffer1 uses Mask RXM1 and filters RXF2, RXF3, RXF4, RXF5

  RXM1SIDL = 0b11100000;
  RXM1SIDH = 0b11111111;	// Set Mask1 to 0x7ff

  // Filters: low byte bits 7..5 are the low 3 bits of the filter, and bits 4..0 are all zeros
  //          high byte bits 7..0 are the high 8 bits of the filter
  //          So total is 11 bits

  // N.B. This is a very wasteful use of filters, but good enough for proof-of-concept
  //      Final implementation will most likely have to use masks to better affect

  // Filter 2: Used for ID#0x206 (SOC)
  RXF2SIDL = 0b11000000;	// Setup Filter2 so that CAN ID 0x206 will be accepted
  RXF2SIDH = 0b01000000;

  // Filter 3: Used for ID#0x4E1
  RXF3SIDL = 0b00100000;	// Setup Filter3 so that CAN ID 0x4E1 will be accepted
  RXF3SIDH = 0b10011100;

  // Filter 4: Used for ID#0x514
  RXF4SIDL = 0b10000000;        // Setup Filter4 so that CAN ID 0x514 will be accepted
  RXF4SIDH = 0b10100010;

  BRGCON1 = 0x01; // SET BAUDRATE to 500 Kbps
  BRGCON2 = 0xD2;
  BRGCON3 = 0x02;

  CIOCON = 0b00100000; // CANTX pin will drive VDD when recessive
  if (sys_features[FEATURE_CANWRITE]>0)
    {
    CANCON = 0b00000000;  // Normal mode
    }
  else
    {
    CANCON = 0b01100000; // Listen only mode, Receive bufer 0
    }

  // Hook in...
  vehicle_fn_poll0 = &vehicle_voltampera_poll0;
  vehicle_fn_poll1 = &vehicle_voltampera_poll1;
  vehicle_fn_ticker1 = &vehicle_voltampera_ticker1;
  
  net_fnbits |= NET_FN_INTERNALGPS;   // Require internal GPS

  return TRUE;
  }
