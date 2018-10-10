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
  #define BUGS3MINI_Cmds PROTO_Cmds
  #pragma long_calls
#endif
#include "common.h"
#include "interface.h"
#include "mixer.h"
#include "config/model.h"
#include "config/tx.h" // for Transmitter
#include "music.h"

#ifdef MODULAR
  //Some versions of gcc applythis to definitions, others to calls
  //So just use long_calls everywhere
  //#pragma long_calls_off
  extern unsigned _data_loadaddr;
  const unsigned long protocol_type = (unsigned long)&_data_loadaddr;
#endif

#ifdef PROTO_HAS_NRF24L01

#include "iface_nrf24l01.h"

#ifdef EMULATOR
    #define dbgprintf printf
#else
    #define dbgprintf if(0) printf
#endif

#define INITIAL_WAIT    500
#define PACKET_INTERVAL 6840
#define TX_PAYLOAD_SIZE 24
#define RX_PAYLOAD_SIZE 16
#define NUM_RF_CHANNELS 15
#define ADDRESS_SIZE    5

static u8 packet[TX_PAYLOAD_SIZE];
static u8 current_chan;
static u8 tx_rx_addr[ADDRESS_SIZE];
static u8 rxid[2];
static u8 phase;
static u8 tx_power;
static u8 armed, arm_flags;
static u8 arm_channel_previous;

static const u8 bind_chans[NUM_RF_CHANNELS] = {0x1A,0x23,0x2C,0x35,0x3E,0x17,0x20,0x29,0x32,0x3B,0x14,0x1D,0x26,0x2F,0x38}; // bugs 3 mini
//const u8 bind_chans[NUM_RF_CHANNELS] = {0x1A,0x23,0x2C,0x35,0x17,0x20,0x29,0x32,0x3B,0x14,0x1D,0x26,0x2F,0x38,0x11}; // bugs 3 H

// dumped from tx #1 (C0ckpitvue 777)
//const u8 rf_chans[NUM_RF_CHANNELS] = {0x22,0x2f,0x3a,0x14,0x20,0x2d,0x38,0x18,0x26,0x32,0x11,0x1d,0x29,0x35,0x17};
//const u8 txid[3] = {0xA8,0xE6,0x32};
//const u8 tx_hash = 0x6c;

// dumped from tx #2 (DPyro)
//const u8 rf_chans[NUM_RF_CHANNELS] = {0x3d,0x34,0x2b,0x22,0x19,0x40,0x37,0x2e,0x25,0x1c,0x3a,0x31,0x28,0x1f,0x16};
//const u8 txid[3] = {0xdd,0xab,0xfd};
//const u8 tx_hash = 0x9e;

// dumped from tx #3 (goebish)
static const u8 rf_chans[NUM_RF_CHANNELS] = {0x12,0x20,0x2f,0x1a,0x28,0x38,0x14,0x23,0x32,0x1c,0x2c,0x3b,0x17,0x26,0x34};
static const u8 txid[3] = {0x90,0x9e,0x4a};
static const u8 tx_hash = 0x3d;

// dumped from Bugs 3H (aszasza)
//const u8 rf_chans[NUM_RF_CHANNELS] = {0x13,0x25,0x37,0x1F,0x31,0x17,0x28,0x3A,0x1C,0x2E,0x22,0x33,0x19,0x2B,0x3D};
//const u8 txid[3] = {0x20,0x28,0xBA};
//const u8 tx_hash = 0xb3;

enum {
    BIND1,
    BIND2,
    DATA
};

// For code readability
enum {
    CHANNEL1 = 0,
    CHANNEL2,
    CHANNEL3,
    CHANNEL4,
    CHANNEL5,
    CHANNEL6,
    CHANNEL7,
    CHANNEL8,
    CHANNEL9,
    CHANNEL10
};

#define CHANNEL_ARM         CHANNEL5
#define CHANNEL_LED         CHANNEL6
#define CHANNEL_FLIP        CHANNEL7
#define CHANNEL_PICTURE     CHANNEL8
#define CHANNEL_VIDEO       CHANNEL9
#define CHANNEL_ANGLE       CHANNEL10

// flags packet[12]
#define FLAG_FLIP    0x08    // automatic flip
#define FLAG_MODE    0x04    // low/high speed select (set is high speed)
#define FLAG_VIDEO   0x02    // toggle video
#define FLAG_PICTURE 0x01    // toggle picture

// flags packet[13]
#define FLAG_LED     0x80    // enable LEDs
#define FLAG_ARM     0x40    // arm (toggle to turn on motors)
#define FLAG_DISARM  0x20    // disarm (toggle to turn off motors)
#define FLAG_ANGLE   0x04    // angle/acro mode (set is angle mode)

static const char *const bugs3mini_opts[] = {
    _tr_noop("RX Id"), "-32768", "32767", "1", NULL,
    _tr_noop("Address"), "-32768", "32767", "655361", NULL,
    NULL
};

enum {
    PROTOOPTS_RXID = 0,
    PROTOOPTS_ADDR,
    LAST_PROTO_OPT
};

ctassert(LAST_PROTO_OPT <= NUM_PROTO_OPTS, too_many_protocol_opts);

// Bit vector from bit position
#define BV(bit) (1 << bit)

static u8 checksum()
{
    u8 checksum = 0x6d;
    for(u8 i=1; i < TX_PAYLOAD_SIZE; i++)
        checksum ^= packet[i];
    return checksum;
}

static void bugs3mini_init()
{
    tx_power = Model.tx_power;
    NRF24L01_Initialize();
    NRF24L01_SetTxRxMode(TX_EN);
    NRF24L01_FlushTx();
    NRF24L01_FlushRx();
    NRF24L01_WriteReg(NRF24L01_07_STATUS, 0x70);     // Clear data ready, data sent, and retransmit
    NRF24L01_WriteReg(NRF24L01_01_EN_AA, 0x00);      // No Auto Acknowldgement on all data pipes
    NRF24L01_WriteReg(NRF24L01_02_EN_RXADDR, 0x01);  // Enable data pipe 0 only
    NRF24L01_WriteReg(NRF24L01_11_RX_PW_P0, RX_PAYLOAD_SIZE); // bytes of data payload for rx pipe 1
    NRF24L01_WriteReg(NRF24L01_06_RF_SETUP, 0x07);
    NRF24L01_SetBitrate(NRF24L01_BR_1M);
    NRF24L01_SetPower(tx_power);
    NRF24L01_Activate(0x73);                          // Activate feature register
    NRF24L01_WriteReg(NRF24L01_1C_DYNPD, 0x00);       // Disable dynamic payload length on all pipes
    NRF24L01_WriteReg(NRF24L01_1D_FEATURE, 0x00);     // Set feature bits on

    // Check for Beken BK2421/BK2423 chip
    // It is done by using Beken specific activate code, 0x53
    // and checking that status register changed appropriately
    // There is no harm to run it on nRF24L01 because following
    // closing activate command changes state back even if it
    // does something on nRF24L01
    NRF24L01_Activate(0x53); // magic for BK2421 bank switch
    dbgprintf("Trying to switch banks\n");
    if (NRF24L01_ReadReg(NRF24L01_07_STATUS) & 0x80) {
        dbgprintf("BK2421 detected\n");
        // Beken registers don't have such nice names, so we just mention
        // them by their numbers
        // It's all magic, eavesdropped from real transfer and not even from the
        // data sheet - it has slightly different values
        NRF24L01_WriteRegisterMulti(0x00, (u8 *) "\x40\x4B\x01\xE2", 4);
        NRF24L01_WriteRegisterMulti(0x01, (u8 *) "\xC0\x4B\x00\x00", 4);
        NRF24L01_WriteRegisterMulti(0x02, (u8 *) "\xD0\xFC\x8C\x02", 4);
        NRF24L01_WriteRegisterMulti(0x03, (u8 *) "\x99\x00\x39\x21", 4);
        NRF24L01_WriteRegisterMulti(0x04, (u8 *) "\xD9\x96\x82\x1B", 4);
        NRF24L01_WriteRegisterMulti(0x05, (u8 *) "\x24\x06\x7F\xA6", 4);
        NRF24L01_WriteRegisterMulti(0x0C, (u8 *) "\x00\x12\x73\x00", 4);
        NRF24L01_WriteRegisterMulti(0x0D, (u8 *) "\x46\xB4\x80\x00", 4);
        NRF24L01_WriteRegisterMulti(0x04, (u8 *) "\xDF\x96\x82\x1B", 4);
        NRF24L01_WriteRegisterMulti(0x04, (u8 *) "\xD9\x96\x82\x1B", 4);
    } else {
        dbgprintf("nRF24L01 detected\n");
    }
    NRF24L01_Activate(0x53); // switch bank back
}

#define CHAN_RANGE (CHAN_MAX_VALUE - CHAN_MIN_VALUE)
static u16 scale_channel(u8 ch, u16 destMin, u16 destMax)
{
    s32 chanval = Channels[ch];
    s32 range = (s32) destMax - (s32) destMin;

    if (chanval < CHAN_MIN_VALUE)
        chanval = CHAN_MIN_VALUE;
    else if (chanval > CHAN_MAX_VALUE)
        chanval = CHAN_MAX_VALUE;
    return (range * (chanval - CHAN_MIN_VALUE)) / CHAN_RANGE + destMin;
}

static void check_arming(s32 channel_value) {
    u8 arm_channel = channel_value > 0;

    if (arm_channel != arm_channel_previous) {
        arm_channel_previous = arm_channel;
        if (arm_channel) {
            armed = 1;
            arm_flags ^= FLAG_ARM;
        } else {
            armed = 0;
            arm_flags ^= FLAG_DISARM;
        }
    }
}

#define GET_FLAG(ch, mask) (Channels[ch] > 0 ? mask : 0)
static void send_packet(u8 bind)
{
    u16 aileron = scale_channel(CHANNEL1, 0, 500);
    u16 elevator = scale_channel(CHANNEL2, 0, 500);
    u16 throttle = scale_channel(CHANNEL3, 0, 500);
    u16 rudder = scale_channel(CHANNEL4, 0, 500);
    
    check_arming(Channels[CHANNEL_ARM]);  // sets globals arm_flags and armed
    
    packet[1] = txid[0];
    packet[2] = txid[1];
    packet[3] = txid[2];
    if(bind) {
        packet[4] = 0x00;
        packet[5] = 0x7d;
        packet[6] = 0x7d;
        packet[7] = 0x7d;
        packet[8] = 0x20;
        packet[9] = 0x20;
        packet[10]= 0x20;
        packet[11]= 0x40;
        packet[12]^= 0x40; // alternating freq hopping flag
        packet[13]= 0x60;
        packet[14]= 0x00;
        packet[15]= 0x00;
    }
    else {
        packet[4] = throttle >> 1;
        packet[5] = rudder >> 1;
        packet[6] = elevator >> 1;
        packet[7] = aileron >> 1;
        packet[8] = 0x20 | (aileron << 8);
        packet[9] = 0x20 | (elevator << 8);
        packet[10]= 0x20 | (rudder << 8);
        packet[11]= 0x40 | (throttle << 8); // 4e ?
        packet[12]= 0x80 | (packet[12] & 0x40 ? 0 : 0x40) // bugs 3 H doesn't have 0x80 ?
                  | FLAG_MODE
                  | GET_FLAG(CHANNEL_FLIP, FLAG_FLIP)
                  | GET_FLAG(CHANNEL_PICTURE, FLAG_PICTURE)
                  | GET_FLAG(CHANNEL_VIDEO, FLAG_VIDEO);
        packet[13] = arm_flags
                   | GET_FLAG(CHANNEL_LED, FLAG_LED)
                   | GET_FLAG(CHANNEL_ANGLE, FLAG_ANGLE);
        
        packet[14] = 0;
        packet[15] = 0; // 0x53 on bugs 3 H ?
    }
    packet[0] = checksum();
    
    if(!(packet[12]&0x40)) {
        current_chan++;
        if(current_chan >= NUM_RF_CHANNELS)
            current_chan = 0;
        NRF24L01_WriteReg(NRF24L01_05_RF_CH, bind ? bind_chans[current_chan] : rf_chans[current_chan]);
    }
    
    // Power on, TX mode, 2byte CRC
    XN297_Configure(BV(NRF24L01_00_EN_CRC) | BV(NRF24L01_00_CRCO) | BV(NRF24L01_00_PWR_UP));
    NRF24L01_WriteReg(NRF24L01_07_STATUS, 0x70);
    NRF24L01_FlushTx();
    XN297_WritePayload(packet, TX_PAYLOAD_SIZE);
    
    if (tx_power != Model.tx_power) {
        //Keep transmit power updated
        tx_power = Model.tx_power;
        NRF24L01_SetPower(tx_power);
    }
}

MODULE_CALLTYPE
static u16 bugs3mini_callback()
{
    switch(phase) {
        case BIND1:
            if( NRF24L01_ReadReg(NRF24L01_07_STATUS) & BV(NRF24L01_07_RX_DR)) { // RX fifo data ready
                XN297_ReadPayload(packet, RX_PAYLOAD_SIZE);
                rxid[0] = packet[1];
                rxid[1] = packet[2];
                Model.proto_opts[PROTOOPTS_RXID] = (u16)rxid[0]<<8 | rxid[1]; // store rxid into protocol options
                NRF24L01_SetTxRxMode(TXRX_OFF);
                NRF24L01_SetTxRxMode(TX_EN);
                
                tx_rx_addr[0] = rxid[0];
                tx_rx_addr[1] = tx_hash;
                tx_rx_addr[2] = rxid[1];
                tx_rx_addr[3] = Model.proto_opts[PROTOOPTS_ADDR] >> 8;
                tx_rx_addr[4] = Model.proto_opts[PROTOOPTS_ADDR] & 0xff;
                
                XN297_SetTXAddr(tx_rx_addr, 5);
                XN297_SetRXAddr(tx_rx_addr, 5);
                
                phase = DATA;
                PROTOCOL_SetBindState(0);
                return PACKET_INTERVAL;
            }
            NRF24L01_SetTxRxMode(TXRX_OFF);
            NRF24L01_SetTxRxMode(TX_EN);
            send_packet(1);
            phase = BIND2;
            return 1000;
        case BIND2:
            // switch to RX mode
            NRF24L01_SetTxRxMode(TXRX_OFF);
            NRF24L01_SetTxRxMode(RX_EN);
            NRF24L01_FlushRx();
            XN297_Configure(BV(NRF24L01_00_EN_CRC) | BV(NRF24L01_00_CRCO) 
                          | BV(NRF24L01_00_PWR_UP) | BV(NRF24L01_00_PRIM_RX));
            phase = BIND1;
            return PACKET_INTERVAL - 1000;
        case DATA:
            send_packet(0);
            break;
    }
    return PACKET_INTERVAL;
}

static void initialize(u8 bind)
{
    CLOCK_StopTimer();
    memset(packet, (u8)0, sizeof(packet));
    bugs3mini_init();
    if(bind) {
        phase = BIND1;
        XN297_SetTXAddr((const u8*)"mjxRC", 5);
        XN297_SetRXAddr((const u8*)"mjxRC", 5);
        PROTOCOL_SetBindState(0xFFFFFFFF);
    }
    else {
        rxid[0] = Model.proto_opts[PROTOOPTS_RXID] >> 8;
        rxid[1] = Model.proto_opts[PROTOOPTS_RXID] & 0xff;
        
        tx_rx_addr[0] = rxid[0];
        tx_rx_addr[1] = tx_hash;
        tx_rx_addr[2] = rxid[1];
        tx_rx_addr[3] = Model.proto_opts[PROTOOPTS_ADDR] >> 8;
        tx_rx_addr[4] = Model.proto_opts[PROTOOPTS_ADDR] & 0xff;
                
        XN297_SetTXAddr(tx_rx_addr, 5);
        XN297_SetRXAddr(tx_rx_addr, 5);
        
        phase = DATA;
    }
    armed = 0;
    arm_flags = FLAG_DISARM;    // initial value from captures
    arm_channel_previous = Channels[CHANNEL_ARM] > 0;
    CLOCK_StartTimer(INITIAL_WAIT, bugs3mini_callback);
}

const void *BUGS3MINI_Cmds(enum ProtoCmds cmd)
{
    switch(cmd) {
        case PROTOCMD_INIT:  initialize(0); return 0;
        case PROTOCMD_DEINIT:
        case PROTOCMD_RESET:
            CLOCK_StopTimer();
            return (void *)(A7105_Reset() ? 1L : -1L);
        case PROTOCMD_CHECK_AUTOBIND: return 0;
        case PROTOCMD_BIND:  initialize(1); return 0;
        case PROTOCMD_NUMCHAN: return (void *)8L;
        case PROTOCMD_DEFAULT_NUMCHAN: return (void *)8L;
        case PROTOCMD_CURRENT_ID: return Model.fixed_id ? (void *)((unsigned long)Model.fixed_id) : 0;
        case PROTOCMD_GETOPTIONS: return bugs3mini_opts;
        case PROTOCMD_TELEMETRYSTATE:
            return (void *)(long) PROTO_TELEM_ON;
        case PROTOCMD_TELEMETRYTYPE:
            return (void *)(long) TELEM_FRSKY;
        default: break;
    }
    return 0;
}

#endif