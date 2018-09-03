/*
 This project is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Deviation is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Deviation.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifdef MODULAR
  //Allows the linker to properly relocate
  #define Hitec_Cmds PROTO_Cmds
  #pragma long_calls
#endif
#include "common.h"
#include "interface.h"
#include "mixer.h"
#include "config/model.h"
#include "config/tx.h"
#include "telemetry.h"

#ifdef MODULAR
  //Some versions of gcc applythis to definitions, others to calls
  //So just use long_calls everywhere
  //#pragma long_calls_off
  extern unsigned _data_loadaddr;
  const unsigned long protocol_type = (unsigned long)&_data_loadaddr;
#endif

#ifdef PROTO_HAS_CC2500

#include "iface_cc2500.h"

//#define HITEC_FORCE_ID    //Use the ID and hopping table from the original dump

#define HITEC_PACKET_LEN        13
#define HITEC_TX_ID_LEN         2
#define HITEC_BIND_COUNT        444 // 10sec
#define HITEC_NUM_FREQUENCE     21
#define HITEC_BIND_NUM_FREQUENCE 14

enum {
    HITEC_START = 0x00,
    HITEC_CALIB = 0x01,
    HITEC_PREP  = 0x02,
    HITEC_DATA1 = 0x03,
    HITEC_DATA2 = 0x04,
    HITEC_DATA3 = 0x05,
    HITEC_DATA4 = 0x06,
    HITEC_RX1   = 0x07,
    HITEC_RX2   = 0x08,
};

static const char *const hitec_opts[] = {
    _tr_noop("Format"), "HITEC", "OPTIMA", NULL,
    _tr_noop("Freq-Fine"),  "-127", "127", NULL,
    NULL
};

enum {
    FORMAT_HITEC,
    FORMAT_OPTIMA,
};

enum {
    PROTO_OPTS_FORMAT = 0,
    PROTO_OPTS_FREQFINE,
    LAST_PROTO_OPT,
};

ctassert(LAST_PROTO_OPT <= NUM_PROTO_OPTS, too_many_protocol_opts);


static u16 bind_counter;
static u8  rx_tx_addr[5];
static u8  phase;
static u8  bind_in_progress;
static u8  bind_phase;
static u8  hopping_frequency[50];
static u8  hopping_frequency_no;
static u8  packet[40];
static u8 calData[48];
static u8  rf_ch_num;
#define MAX_PKT 29
static u8 pkt[MAX_PKT];//telemetry receiving packets
static s8 fine;
static u8 F5_frame;
static u8 F5_counter;


#define HITEC_FREQ0_VAL 0xE8
static void HITEC_CC2500_init() {
    const u8 HITEC_init_values[] = {
      /* 00 */ 0x2F, 0x2E, 0x2F, 0x07, 0xD3, 0x91, 0xFF, 0x04,
      /* 08 */ 0x45, 0x00, 0x00, 0x12, 0x00, 0x5C, 0x85, HITEC_FREQ0_VAL,
      /* 10 */ 0x3D, 0x3B, 0x73, 0x73, 0x7A, 0x01, 0x07, 0x30,
      /* 18 */ 0x08, 0x1D, 0x1C, 0xC7, 0x00, 0xB0, 0x87, 0x6B,
      /* 20 */ 0xF8, 0xB6, 0x10, 0xEA, 0x0A, 0x00, 0x11,
    };

    CC2500_Strobe(CC2500_SIDLE);

    for (u8 i = 0; i < 39; ++i)
        CC2500_WriteReg(i, HITEC_init_values[i]);

    CC2500_SetTxRxMode(TX_EN);
    CC2500_SetPower(Model.tx_power);
}

// Generate RF channels
static void HITEC_RF_channels()
{
    //Normal hopping
    u8 idx = 0;
    u32 rnd = CLOCK_getms();

    while (idx < HITEC_NUM_FREQUENCE)
    {
        u8 i;
        u8 count_0_47 = 0, count_48_93 = 0, count_94_140 = 0;

        rnd = rnd * 0x0019660D + 0x3C6EF35F; // Randomization
        // Use least-significant byte and make sure it's pair.
        u8 next_ch = ((rnd >> 8) % 141) & 0xFE;
        // Check that it's not duplicated and spread uniformly
        for (i = 0; i < idx; i++) {
            if(hopping_frequency[i] == next_ch)
                break;
            if(hopping_frequency[i] <= 47)
                count_0_47++;
            else if (hopping_frequency[i] <= 93)
                count_48_93++;
            else
                count_94_140++;
        }
        if (i != idx)
            continue;
        if ( (next_ch <= 47 && count_0_47 < 8) || (next_ch >= 48 && next_ch <= 93 && count_48_93 < 8) || (next_ch >= 94 && count_94_140 < 8) )
            hopping_frequency[idx++] = next_ch;//find hopping frequency
    }
}

static void HITEC_tune_chan()
{
    CC2500_Strobe(CC2500_SIDLE);
    if(bind_in_progress)
        CC2500_WriteReg(CC2500_0A_CHANNR, hopping_frequency_no*10);
    else
        CC2500_WriteReg(CC2500_0A_CHANNR, hopping_frequency[hopping_frequency_no]);
    CC2500_Strobe(CC2500_SFTX);
    CC2500_Strobe(CC2500_SCAL);
    CC2500_Strobe(CC2500_STX);
}

static void HITEC_change_chan_fast()
{
    CC2500_Strobe(CC2500_SIDLE);
    if(bind_in_progress)
        CC2500_WriteReg(CC2500_0A_CHANNR, hopping_frequency_no*10);
    else
        CC2500_WriteReg(CC2500_0A_CHANNR, hopping_frequency[hopping_frequency_no]);
    CC2500_WriteReg(CC2500_25_FSCAL1, calData[hopping_frequency_no]);
}

// Channel value -125%<->125% is scaled to 16bit value with no limit
#define CHANNEL_MAX_100 1844    //  100%
#define CHANNEL_MIN_100 204     //  100%
u16 convert_channel_16b_nolimit(uint8_t num, int16_t min, int16_t max)
{
    s32 val = Channels[num] * 1024 / CHAN_MAX_VALUE + 1024;    // 0<->2047
    val=(val-CHANNEL_MIN_100)*(max-min)/(CHANNEL_MAX_100-CHANNEL_MIN_100)+min;
    return (u16)val;
}

// Channel values are 12-bit values between 1020 and 2020, 1520 is the middle.
// Futaba @140% is 2070...1520...970
// Values grow down and to the right.
static void HITEC_build_packet()
{
    u8 offset;
                    
    packet[1] = rx_tx_addr[1];
    packet[2] = rx_tx_addr[2];
    packet[3] = rx_tx_addr[3];
    packet[22] = 0xEE;  // unknown always 0xEE
    if(bind_in_progress)
    {
        packet[0] = 0x16;   // 22 bytes to follow
        memset(packet+5,0x00,14);
        switch(bind_phase)
        {
            case 0x72:          // first part of the hopping table
                for(u8 i=0;i<14;i++)
                    packet[5+i]=hopping_frequency[i]>>1;
                break;
            case 0x73:          // second part of the hopping table
                for(u8 i=0;i<7;i++)
                    packet[5+i]=hopping_frequency[i+14]>>1;
                break;
            case 0x74:
                packet[7]=0x55; // unknown but bind does not complete if not there
                packet[8]=0x55; // unknown but bind does not complete if not there
                break;
            case 0x7B:
                packet[5]=hopping_frequency[13]>>1; // if not there the Optima link is jerky...
                break;
        }
        if (Model.proto_opts[PROTO_OPTS_FORMAT] == FORMAT_OPTIMA) {
            packet[4] = bind_phase; // increments based on RX answer
        } else {
            packet[4] = bind_phase+0x10;
            bind_phase^=0x01;       // switch between 0x82=first part of the hopping table and 0x83=second part
        }
        packet[19] = 0x08;  // packet number
        offset=20;          // packet[20] and [21]
    }
    else
    {
        packet[0] = 0x1A;   // 26 bytes to follow
        for(u8 i=0;i<9;i++)
        {
            u16 ch = convert_channel_16b_nolimit(i,0x1B87,0x3905);
            packet[4+2*i] = ch >> 8;
            packet[5+2*i] = ch & 0xFF;
        }
        packet[23] = 0x80;  // packet number
        offset=24;          // packet[24] and [25]
        packet[26] = 0x00;  // unknown always 0 and the RX doesn't seem to care about the value?
    }

	if(F5_frame)
	{// No idea what it is but Minima RXs are expecting these frames to work to work
		packet[offset] = 0xF5;
		packet[offset+1] = 0xDF;
		if((F5_counter%9)==0)
			packet[offset+1] -= 0x04;	// every 8 packets send 0xDB
		F5_counter++;
		F5_counter%=59;					// every 6 0xDB packets wait only 4 to resend instead of 8
		F5_frame=0;					// alternate
		if(bind_in_progress)
			packet[offset+1]++;			// when binding the values are 0xE0 and 0xDC
	}
	else
	{
		packet[offset] = 0x00;
		packet[offset+1] = 0x00;
		F5_frame=1;					// alternate
	}
/*  debug("P:");
    for(u8 i=0;i<packet[0]+1;i++)
        debug("%02X,",packet[i]);
    debugln("");
*/
}

static void HITEC_send_packet()
{
    CC2500_WriteData(packet, packet[0]+1);
    if(bind_in_progress)
        packet[19] >>= 1;   // packet number
    else
        packet[23] >>= 1;   // packet number
}

static void update_telemetry(u8 *pkt, u8 len) {
  Telemetry.value[TELEM_FRSKY_RSSI] = pkt[len-2] ^ 0x80;
  TELEMETRY_SetUpdated(TELEM_FRSKY_RSSI);
  Telemetry.value[TELEM_FRSKY_LQI] = pkt[len-1] & 0x7F;
  TELEMETRY_SetUpdated(TELEM_FRSKY_LQI);
  Telemetry.value[TELEM_FRSKY_VOLT1] = ((pkt[len-3]<<8) + pkt[len-4])*1000/28;    // calculation in decimal is volt=(pkt[len-3]<<8+pkt[len-4])/28
  TELEMETRY_SetUpdated(TELEM_FRSKY_VOLT1);
}

static u16 ReadHitec()
{
    u8 len;

    switch(phase)
    {
        case HITEC_START:
            HITEC_CC2500_init();
            bind_phase=0x72;
            if(bind_in_progress)
            {
                bind_counter = HITEC_BIND_COUNT;
                rf_ch_num=HITEC_BIND_NUM_FREQUENCE;
            }
            else
            {
                bind_counter=0;
                rf_ch_num=HITEC_NUM_FREQUENCE;
                //Set TXID
                CC2500_WriteReg(CC2500_05_SYNC0,rx_tx_addr[2]);
                CC2500_WriteReg(CC2500_04_SYNC1,rx_tx_addr[3]);
            }
            hopping_frequency_no=0;
            HITEC_tune_chan();
            phase = HITEC_CALIB;
            return 2000;
        case HITEC_CALIB:
            calData[hopping_frequency_no]=CC2500_ReadReg(CC2500_25_FSCAL1);
            hopping_frequency_no++;
            if (hopping_frequency_no < rf_ch_num)
                HITEC_tune_chan();
            else
            {
                hopping_frequency_no = 0;
                phase = HITEC_PREP;
            }
            return 2000;

        /* Work cycle: 22.5ms */
#define HITEC_PACKET_PERIOD 22500
#define HITEC_PREP_TIMING   462
#define HITEC_DATA_TIMING   2736
#define HITEC_RX1_TIMING    4636
        case HITEC_PREP:
            if (fine == (s8)Model.proto_opts[PROTO_OPTS_FREQFINE]) {    // No user frequency change
                HITEC_change_chan_fast();
                hopping_frequency_no++;
                if(hopping_frequency_no>=rf_ch_num)
                    hopping_frequency_no=0;
                CC2500_SetPower(Model.tx_power);
                CC2500_SetTxRxMode(TX_EN);
                HITEC_build_packet();
                phase++;
            } else {
                fine = (s8)Model.proto_opts[PROTO_OPTS_FREQFINE];
                CC2500_WriteReg(CC2500_0C_FSCTRL0, fine);
                phase = HITEC_START;    // Restart the tune process if option is changed to get good tuned values
            }
            return HITEC_PREP_TIMING;
        case HITEC_DATA1:
        case HITEC_DATA2:
        case HITEC_DATA3:
        case HITEC_DATA4:
            HITEC_send_packet();
            phase++;
            return HITEC_DATA_TIMING;
        case HITEC_RX1:
            CC2500_SetTxRxMode(RX_EN);
            CC2500_Strobe(CC2500_SRX);  // Turn RX ON
            phase++;
            return HITEC_RX1_TIMING;
        case HITEC_RX2:
            len=CC2500_ReadReg(CC2500_3B_RXBYTES | CC2500_READ_BURST) & 0x7F;
            if(len && len<MAX_PKT)
            { // Something has been received
                CC2500_ReadData(pkt, len);
                if( (pkt[len-1] & 0x80) && pkt[0]==len-3 && pkt[1]==rx_tx_addr[1] && pkt[2]==rx_tx_addr[2] && pkt[3]==rx_tx_addr[3])
                { //valid crc && length ok && tx_id ok
                    //debug("RX:l=%d",len);
                    //for(u8 i=0;i<len;i++)
                    //  debug(",%02X",pkt[i]);
                    if(bind_in_progress)
                    {
                        if(len==13) // Bind packets have a length of 13
                        { // bind packet: 0A,00,E5,F2,7X,05,06,07,08,09,00
                            //debug(",bind");
                            u8 check=1;
                            for(u8 i=5;i<=10;i++)
                                if(pkt[i]!=i%10) check=0;
                            if((pkt[4]&0xF0)==0x70 && check)
                            {
                                bind_phase=pkt[4]+1;
                                if(bind_phase==0x7B)
                                    bind_counter=164;   // in dumps the RX stops to reply at 0x7B so wait a little and exit
                            }
                        }
                    }
                    else
                        if(len==15) // Telemetry packets have a length of 15
                        { // telem packet: 0C, 00, 6A, 03, 00, 00, 00, 00, 00, 00, 00, 8E, 00, 5E, 94 
                          //Not fully handled since the RX I have only send 1 frame where from what I've been reading on Hitec telemetry should have at least 4 frames...
                            //debug(",telem");
                            update_telemetry(pkt, len);
                        }
                    //debugln("");
                }
            }
            CC2500_Strobe(CC2500_SFRX);  // Flush the RX FIFO buffer
            phase = HITEC_PREP;
            if(bind_counter)
            {
                bind_counter--;
                if(!bind_counter)
                {
                    bind_in_progress = 0;
                    phase=HITEC_START;
                }
            }
            return (HITEC_PACKET_PERIOD -HITEC_PREP_TIMING -4*HITEC_DATA_TIMING -HITEC_RX1_TIMING);
    }
    return 0;
}

static u16 initHITEC()
{
    HITEC_RF_channels();
    #ifdef HITEC_FORCE_ID
    // ID and channels taken from dump
		rx_tx_addr[1]=0x00;
		rx_tx_addr[2]=0x03;
        rx_tx_addr[3]=0x6A;
        memcpy((void *)hopping_frequency,
               (void *)"\x00\x3A\x4A\x32\x0C\x58\x2A\x10\x26\x20\x08\x60\x68\x70\x78\x80\x88\x56\x5E\x66\x6E",
               HITEC_NUM_FREQUENCE);
    #endif
    phase = HITEC_START;
    return 10000;
}

MODULE_CALLTYPE
static u16 hitec_cb() {
  return ReadHitec();
}

static void initialize(u8 bind)
{
  CLOCK_StopTimer();
  bind_in_progress = bind;

  fine = (s8)Model.proto_opts[PROTO_OPTS_FREQFINE];
  CC2500_WriteReg(CC2500_0C_FSCTRL0, fine);

  CLOCK_StartTimer(initHITEC(), hitec_cb);
}

const void *Hitec_Cmds(enum ProtoCmds cmd)
{
    switch(cmd) {
        case PROTOCMD_INIT:  initialize(0); return 0;
        case PROTOCMD_DEINIT:
        case PROTOCMD_RESET:
            CLOCK_StopTimer();
            return (void *)(CC2500_Reset() ? 1L : -1L);
        case PROTOCMD_BIND:  initialize(1); return 0;
        case PROTOCMD_NUMCHAN: return (void *)8L;
        case PROTOCMD_DEFAULT_NUMCHAN: return (void *)8L;
        case PROTOCMD_CURRENT_ID: return (void *)((long)Model.fixed_id);
        case PROTOCMD_GETOPTIONS: return hitec_opts;
        case PROTOCMD_TELEMETRYSTATE:
            return (void *)(long)(PROTO_TELEM_ON);
        case PROTOCMD_TELEMETRYTYPE: 
            return (void *)(long) TELEM_FRSKY;
        default: break;
    }
    return 0;
}
#endif
