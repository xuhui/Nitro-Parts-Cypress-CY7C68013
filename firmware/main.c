/**
 * Copyright (C) 2009 Ubixum, Inc. 
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 **/

#include <fx2macros.h>

#include "firmware.h"
#include "vendor_commands.h"
#include <terminals.h> // only need this for prom serialnum, but it should come from project terminals not local fx2_terminals.h
#include "handlers.h"
#include "fx2term.h"

volatile __bit dorenum=FALSE;


extern __code WORD str_serial_addr;
volatile WORD *str_serial = &str_serial_addr;

extern void on_boot();

__xdata rdwr_data_t rdwr_data;


volatile __xdata WORD in_packet_max = 64; // max size for full speed usb (the default before hi-speed interrupt)
__xdata BYTE i2c_addr_buf[2]; // for reading/writing to the eeprom


void reset_endpoints() {
     RESETFIFO(0x02); 
     OUTPKTEND=0x82; 
     SYNCDELAY(); 
     OUTPKTEND=0x82; 
     SYNCDELAY(); 
     OUTPKTEND=0x82;
     SYNCDELAY();
     OUTPKTEND=0x82;
     SYNCDELAY();
     RESETFIFO(0x06);
     printf ( "Reset Endpoints.\n" );
}


// set *alt_ifc to the current alt interface for ifc
#pragma save
#pragma disable_warning 85 // unused variables we don't care about
__xdata BYTE alt=0;
BOOL handle_get_interface(BYTE i, BYTE* a) {
 *a=alt;
 return TRUE;
}
// return TRUE if you set the interface requested
// NOTE this function should reconfigure and reset the endpoints
// according to the interface descriptors you provided.
BOOL handle_set_interface(BYTE ifc,BYTE alt_ifc) {  
    printf ( "Set Interface.\n" );
    RESETTOGGLE(0x02); 
    RESETTOGGLE(0X86); 
    reset_endpoints();
    alt=alt_ifc;
 return TRUE;
}

// handle getting and setting the configuration
// 1 is the default.  If you support more than one config
// keep track of the config number and return the correct number
// config numbers are set int the dscr file.
// volatile BYTE config=1;
BYTE handle_get_configuration() { 
 return 1;
}
// return TRUE if you handle this request
// NOTE changing config requires the device to reset all the endpoints
BOOL handle_set_configuration(BYTE cfg) { 
 printf ( "Set Configuration.\n" );
 //config=cfg;
 return TRUE;
}
#pragma restore // back to caring about the unused variables


//******************* VENDOR COMMAND HANDLERS **************************

// handle funcs take type, length, value, index
// must return TRUE if we handled command ok
// or FALSE if an error.
typedef BOOL (*VCHandleFunc)(); //(BYTE type,WORD length,WORD value,WORD index);

typedef struct {
 BYTE cmd;
 VCHandleFunc handleFunc;
} vc_handler;



__xdata BYTE cur_io_handler=0;
io_handler_read_func cur_read_handler; // this one has a param and doesn't like not being re-entrant

BOOL handleRDWR () { 

    printf ( "Received RDWR VC\n" );
    printf ( "in_packet_max: %d hs %d\n" , in_packet_max, HISPEED );

    if (SETUP_TYPE != 0x40) return FALSE; 
    if (SETUP_LENGTH() != sizeof(rdwr_data_header)) return FALSE;

    // clear out old transaction artifacts if necessary
    if (io_handlers[cur_io_handler].uninit_handler) {
        io_handlers[cur_io_handler].uninit_handler();
    }

    // clear structure back to 0
    memset(&rdwr_data, 0, sizeof(rdwr_data));

    EP0BCL=0; // arm for transfer
    while ( (EP0CS & bmEPBUSY) && !new_vc_cmd ); // host transfer the data
    if (new_vc_cmd) return FALSE;

    memcpy ( &rdwr_data, EP0BUF, sizeof(rdwr_data_header) );
    rdwr_data.in_progress=TRUE;
   
   
    reset_endpoints(); // clear any old data

    cur_io_handler =0;    
    while (TRUE) {
      if (!io_handlers[cur_io_handler].term_addr || io_handlers[cur_io_handler].term_addr == rdwr_data.h.term_addr) {
        printf ( "Found handlers for %d\n" , rdwr_data.h.term_addr );
        cur_read_handler = io_handlers[cur_io_handler].read_handler; 
//        *cur_write_handler = io_handlers[cur].write_handler;
//        *cur_init_handler = io_handlers[cur].init_handler;
//        *cur_uninit_handler = io_handlers[cur].uninit_handler;
//        *cur_status_handler = io_handlers[cur].status_handler;
//        *cur_chksum_handler = io_handlers[cur].chksum_handler;
        break;
      } 
      ++cur_io_handler;
    }
    if (io_handlers[cur_io_handler].init_handler) {
        return io_handlers[cur_io_handler].init_handler();
    }
     
    return cur_read_handler != NULL && io_handlers[cur_io_handler].write_handler != NULL;
}




BOOL rdwr_stat( ) { 


    if (SETUP_LENGTH() != sizeof(rdwr_data)) return FALSE;

    memcpy ( EP0BUF, &rdwr_data, sizeof(rdwr_data) );
    EP0BCH=0;
    SYNCDELAY();
    EP0BCL=sizeof(rdwr_data);
    return TRUE;

}


BOOL handle_serial () {
    if (SETUP_LENGTH() != 16) return FALSE;
    switch ( SETUP_TYPE ) {

        case 0x40:
            EP0BCL=0; // allow transfer in.
            while ((EP0CS & bmEPBUSY) && !new_vc_cmd ); // make sure the data came in ok.
            if (new_vc_cmd) return FALSE;
                
            i2c_addr_buf[0] = MSB(FX2_PROM_SERIALNUM0_0);
            i2c_addr_buf[1] = LSB(FX2_PROM_SERIALNUM0_0);
            i2c_write ( TERM_FX2_PROM, 2, i2c_addr_buf, 16, EP0BUF ); 
            memcpy ( (__xdata BYTE*)&str_serial_addr, EP0BUF, 16 );
            return TRUE;
       case 0xc0:
            while ((EP0CS & bmEPBUSY) && !new_vc_cmd); 
            if (new_vc_cmd) return FALSE;
            memcpy ( EP0BUF, (__xdata BYTE*)str_serial, 16);
            EP0BCH=0; SYNCDELAY();
            EP0BCL=16;
            return TRUE;
        default:
            return FALSE;
    }

}


BOOL handle_renum() {
    dorenum=TRUE;
    return TRUE;
}

// last element of vc_handlers will have a handler w/ 0 for cmd
vc_handler __code vc_handlers[] = {
//    { VC_HI_REGVAL, handleGETSET },
    { VC_HI_RDWR, handleRDWR },
    { VC_RDWR_STAT, rdwr_stat },
    { VC_RENUM, handle_renum },
    { VC_SERIAL, handle_serial },
    { 0 }
 };

BOOL handle_vendorcommand(BYTE cmd) {
 __xdata BYTE i=0;
 //printf ( "setupdat[5] %02x setupdat[4] %02x\n", SETUPDAT[5], SETUPDAT[4] );
 while (TRUE) {
   if (vc_handlers[i].cmd == cmd) {
       return vc_handlers[i].handleFunc();
   } else if (vc_handlers[i].cmd == 0) {
     break;
   } else {
     ++i;
   }
 } 
 return FALSE; // not handled by handlers

}

io_handler_boot_func boot_func;

void main_init() {
 BYTE c;

 printf ( "Someone Called Init\n" );
 CPUCS &= ~bmCLKOE; // don't drive clkout;
 I2CTL |= bm400KHZ; 
 REVCTL=3;
 IFCONFIG=0xC0; // internal, 48mhz clk, don't drive default.
 
 // initialize the device serial number
 if (EEPROM_TWO_BYTE) { // this will fail anyway if a two byte prom wasn't detected
    i2c_addr_buf[0] = MSB(FX2_PROM_SERIALNUM0_0);
    i2c_addr_buf[1] = LSB(FX2_PROM_SERIALNUM0_0);
    i2c_write( TERM_FX2_PROM, 2, i2c_addr_buf, 0, NULL );
    i2c_read ( TERM_FX2_PROM, 16, (__xdata BYTE*)&str_serial_addr ); 
    printf ( "Read Serial.\n" );
 }

 // other endpoints not valid
 EP1OUTCFG &= ~bmVALID;
 EP4CFG &= ~bmVALID;
 EP8CFG &= ~bmVALID;
  
 EP2CFG = 0xA0;   // 10100000 VALID, OUT, BULK, 512 BYTES, QUAD 
 EP2FIFOCFG = 0; SYNCDELAY();

 EP6CFG = 0xE0; SYNCDELAY();  // 11100000 VALID, IN , BULK, 512 BYTES, QUAD 

 EP6FIFOCFG = 0; SYNCDELAY(); // zero to one transition just in case

 // give in_packet_max a default
 in_packet_max = HISPEED ? HISPD_EP6_SIZE : FULLSPD_EP6_SIZE;

 // any custom boot firmware boot function
 on_boot();

 // any custom handler boot function
 c=0;
 while (TRUE) {
   boot_func = io_handlers[c].boot_handler; 
   if (boot_func) boot_func(io_handlers[c].term_addr);
   if (!io_handlers[c++].term_addr) break;
 }

 printf ( "Initialization Done.\n" );

}

void send_ack_packet() {
    WORD status=rdwr_data.aborted;
    WORD checksum=0;
    if (io_handlers[cur_io_handler].status_handler) status=io_handlers[cur_io_handler].status_handler();
    if (io_handlers[cur_io_handler].chksum_handler) checksum=io_handlers[cur_io_handler].chksum_handler();
    printf ("Transfer Status %d\n", status );
    EP6FIFOBUF[0] = 0x0f;
    EP6FIFOBUF[1] = 0xa5;
    EP6FIFOBUF[2] = LSB(checksum);
    EP6FIFOBUF[3] = MSB(checksum);
    EP6FIFOBUF[4] = LSB(status); // status low
    EP6FIFOBUF[5] = MSB(status); // status high
    EP6FIFOBUF[6] = 0; // reserved low
    EP6FIFOBUF[7] = 0; // reserved high
    EP6BCH=0; SYNCDELAY();
    EP6BCL=8; // send the ack
}


void main_loop() {

 // data to read from a terminal?
 if ( rdwr_data.in_progress && !(rdwr_data.h.command & bmSETWRITE) && !rdwr_data.autocommit) {
    __xdata DWORD readlen = rdwr_data.h.transfer_length - rdwr_data.bytes_read;
    __xdata WORD this_read = readlen > in_packet_max ? in_packet_max : readlen;
    if (this_read>0 && !(EP2468STAT & bmEP6FULL)) { 
        if (!rdwr_data.aborted) {
            cur_read_handler(this_read);
        }
        // cur_read_handler might have aborted the transaction
        // in which case and from here on we just send bogus data.
        if ( rdwr_data.aborted ) {
            rdwr_data.bytes_avail = this_read;
        } 
        if (rdwr_data.bytes_avail) {
         rdwr_data.bytes_read += rdwr_data.bytes_avail;
         if ( EP2468STAT & bmEP6FULL ) {
            printf ( "ep6full!!!\n" );
         }
         EP6BCH=MSB(rdwr_data.bytes_avail);
         SYNCDELAY();
         EP6BCL=LSB(rdwr_data.bytes_avail);
        }
    }

    if ( rdwr_data.bytes_read >= rdwr_data.h.transfer_length && !(EP2468STAT & bmEP6FULL)) {
        printf ( "Finished Read.\n" );
        rdwr_data.in_progress = FALSE;
        send_ack_packet();
    }

 } 
 
 // received data to write to a terminal
 
 if (!(EP2468STAT & bmEP2EMPTY) && !rdwr_data.autocommit) {

     if ( !(rdwr_data.h.command & bmSETWRITE) ) {
        OUTPKTEND = 0x82;
     } else {
        rdwr_data.bytes_avail = MAKEWORD ( EP2BCH, EP2BCL );
        //printf ( "Writing %d Bytes..\n", rdwr_data.bytes_avail );
        if (rdwr_data.aborted || io_handlers[cur_io_handler].write_handler()) { 
         rdwr_data.bytes_written += rdwr_data.bytes_avail;
         OUTPKTEND = 0x82; 
        }

        if ( rdwr_data.bytes_written  >= rdwr_data.h.transfer_length ) {
            rdwr_data.in_progress = FALSE;
            send_ack_packet();
        }
     }

 }

 if (dorenum) {
    printf ( "Received USB renum command.\n" );
    dorenum=FALSE;
    RENUMERATE_UNCOND();
 }


}


