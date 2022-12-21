//
// firmware for Cypress EZ-USB FX2LP
//  Family Basic Keyboard Emulator
//
#include "Fx2.h"
#include "fx2regs.h"
#include "syncdly.h"
#include <stdint.h>

#define TAPE_MODE_LOAD 1
#define TAPE_MODE_SAVE 2

#define COMMAND_KEY 0
#define COMMAND_SAVE 1
#define COMMAND_LOAD 2
#define COMMAND_STOP 3
#define COMMAND_FORCE_STOP 4

static uint8_t keybufLow[10];
static uint8_t keybufHigh[10];
static uint8_t keyPos;
static uint8_t highValue;
static uint8_t tapeMode = 0;
static uint8_t bitIndex = 0;
static uint8_t tapeValue = 0;
static uint8_t tapeCounter;

void GpifInit(void);

void Initialize()
{
    // ----------------------------------------------------------------------
    // CPU Clock
    // ----------------------------------------------------------------------
    // bit7:6 -
    // bit5   1=PortC RD#/WR# Strobe enable
    // bit4:3 00=12MHz, 01=24MHz, 10=48MHz, 11=reserved
    // bit2   1=CLKOUT inverted
    // bit1   1=CLKOUT enable
    // bit0   1=reset
    CPUCS = 0x10; // 0b0001_0000; 48MHz
    SYNCDELAY;
    CKCON = 0x00;
    SYNCDELAY;

    // ----------------------------------------------------------------------
    // Interface Config
    // ----------------------------------------------------------------------
    // bit7   1=Internal clock, 0=External
    // bit6   1=48MHz, 0=30MHz
    // bit5   1=IFCLK out enable
    // bit4   1=IFCLK inverted
    // bit3   1=Async, 0=Sync
    // bit2   1=GPIF GSTATE out enable
    // bit1:0 00=Ports, 01=Reserved, 10=GPIF, 11=Slave FIFO
    // IFCONFIG = 0x00; // 0b0000_0000; Ports
    SYNCDELAY;

    // ----------------------------------------------------------------------
    // Chip Revision Control
    // ----------------------------------------------------------------------
    REVCTL = 0x03; // Recommended setting.
    SYNCDELAY;

    GpifInit();
    // ----------------------------------------------------------------------
    // EP Config
    // ----------------------------------------------------------------------
    // bit7   1=Valid
    // bit6   1=IN, 0=OUT
    // bit5:4 00=Invalid, 01=Isochronous, 10=Bulk(default), 11=Interrupt
    // bit3   1=1024bytes buffer(EP2,6 only), 0=512bytes
    // bit2   -
    // bit1:0 00=Quad, 01=Invalid, 10=Double, 11=Triple
    EP1OUTCFG = 0xa0; // 0b1010_0000  // Valid, Bulk-OUT
    SYNCDELAY;
    EP1INCFG = 0x7f; // disable
    SYNCDELAY;
    EP2CFG &= 0x7f; // disable
    SYNCDELAY;
    EP4CFG &= 0xa2; // Bulk-OUT, 512 byte, Double buffer
    SYNCDELAY;
    EP6CFG = 0xe2; // 0b1110_0010; Bulk-IN, 512bytes Double buffer
    SYNCDELAY;
    EP8CFG &= 0x7f; // Bluk-OUT, 512bytes, Double buffer
    SYNCDELAY;

    // ----------------------------------------------------------------------
    // Autopointer
    // ----------------------------------------------------------------------
    AUTOPTRSETUP = 0x7; // Enable both auto-pointer
    SYNCDELAY;
    // ----------------------------------------------------------------------
    // Start EP1
    // ----------------------------------------------------------------------
    EP1OUTBC = 0x1; // Any value enables EP1 transfer
    SYNCDELAY;
    // ----------------------------------------------------------------------
    // Start EP4
    // ----------------------------------------------------------------------
    EP4BCL = 0x1; // Any value enables EP4 transfer
    SYNCDELAY;

    // ----------------------------------------------------------------------
    // IO Port
    // ----------------------------------------------------------------------
    OEA = 0xF0;
    IOA = 0x00;
    PORTACFG = 0x03; // PA.0 = INT0, PA.1 = INT1

    OED = 0x03;
    IOD = 0x00;

    TCON = 0x05;

    // ----------------------------------------------------------------------
    // GPIF
    // ----------------------------------------------------------------------
    EP2FIFOCFG = 0x04;
    SYNCDELAY;
    EP4FIFOCFG = 0x04;
    SYNCDELAY;
    EP6FIFOCFG = 0x04;
    SYNCDELAY;
    EP8FIFOCFG = 0x04;
    SYNCDELAY;

    EIE = 0x04;    // Enable INT4
    GPIFIE = 0x02; // GPIFWF
    SYNCDELAY;
    INTSETUP = 0x02; // INT4 source is GPIF
}

void Timer0Overflow(void) __interrupt(1)
{
    if (tapeMode == TAPE_MODE_SAVE)
    {
        tapeValue <<= 1;
        tapeValue |= IOA & 0x01;
        bitIndex++;
        if (bitIndex == 8)
        {
            if (!(EP2468STAT & bmEP6FULL))
            {
                //*current_tape_out = value;
                EXTAUTODAT2 = tapeValue;
                // current_tape_out++;
                tapeCounter++;
                if (tapeCounter >= 128)
                {
                    EP6BCH = 0;
                    SYNCDELAY;
                    SYNCDELAY;
                    EP6BCL = tapeCounter;
                    SYNCDELAY;
                    // current_tape_out = EP6FIFOBUF;
                    AUTOPTRH2 = MSB(&EP6FIFOBUF);
                    AUTOPTRL2 = LSB(&EP6FIFOBUF);
                    tapeCounter = 0;
                }
            }
            bitIndex = 0;
            tapeValue = 0;
        }
    }
    else
    { // LOAD
        if (!(EP2468STAT & bmEP4EMPTY))
        {
            if (bitIndex == 0)
            {
                tapeValue = EXTAUTODAT2;
                tapeCounter++;
                if (tapeCounter == EP4BCL)
                {
                    AUTOPTRH2 = MSB(&EP4FIFOBUF);
                    AUTOPTRL2 = LSB(&EP4FIFOBUF);
                    EP4BCL = 0; // arm EP4
                    tapeCounter = 0;
                }
            }
            IOD = (((tapeValue)&0x80) >> 7);
            tapeValue <<= 1;
            bitIndex++;
            if (bitIndex == 8)
            {
                bitIndex = 0;
            }
        }
    }
}

void DataSelectorLowInt(void) __interrupt(2)
{
    keyPos++;
    if (keyPos == 10)
        keyPos = 0;

    if ((IOA & 0x4) == 0)
    {
        IOA = 0xf0;
        highValue = 0xf0;
    }
    else
    {
        IOA = keybufLow[keyPos];
        highValue = keybufHigh[keyPos];
    }
}

void DataSelectorHighInt(void) __interrupt(10)
{
    if (IOA & 0x01)
    { // RESET
        keyPos = 0;
        if (!(IOA & 0x4))
        {
            IOA = 0xf0;
            highValue = 0xf0;
        }
        else
        {
            IOA = keybufLow[0];
            highValue = keybufHigh[0];
        }
    }
    else
    { // SEL High
        IOA = highValue;
    }
    EXIF = 0x0;
    // GPIFIRQ = 0x02;
    INT4CLR = 0x01;
}

void Start32kTimer(void)
{
    tapeCounter = 0;
    bitIndex = 0;

    if (tapeMode == TAPE_MODE_SAVE)
    {
        AUTOPTRH2 = MSB(&EP6FIFOBUF);
        AUTOPTRL2 = LSB(&EP6FIFOBUF);
    }
    else if (tapeMode == TAPE_MODE_LOAD)
    {
        AUTOPTRH2 = MSB(&EP4FIFOBUF);
        AUTOPTRL2 = LSB(&EP4FIFOBUF);
    }

    TMOD = 0x02;                      // GATE0=0 , C/T0=0 (CLKOUT Source), Mode2: 8bit with autoload
    TL0 = (unsigned char)(256 - 125); // 32kHz
    TH0 = (unsigned char)(256 - 125);
    CKCON = 0x00; // CLKOUT / 12
    TCON |= 0x10; // TR0=1 : start Timer0
}

void Stop32kTimer(void)
{
    TCON &= ~(0x10); // TR0=1 : stop Timer0
}

void StartKeyInt(void)
{
    IE |= 0x05;  // Enable INT0, INT1
    EIE |= 0x04; // Enable INT4
}

void StopKeyInt(void)
{
    IE &= ~0x05;  // Disable INT0, INT1
    EIE &= ~0x04; // Disable INT4
}

void ProcessUsbCommand(void)
{
    uint8_t *src = EP1OUTBUF;
    uint8_t len = EP1OUTBC;
    uint8_t i;

    switch (*(src++))
    {
    case COMMAND_KEY: // KEY INFO
        for (i = 0; i < 10; i++)
        {
            keybufLow[i] = *(src++);
        }
        for (i = 0; i < 10; i++)
        {
            keybufHigh[i] = *(src++);
        }
        break;
    case COMMAND_LOAD:
        tapeMode = TAPE_MODE_LOAD;
        StopKeyInt();
        Start32kTimer();
        break;

    case COMMAND_SAVE:
        tapeMode = TAPE_MODE_SAVE;
        StopKeyInt();
        Start32kTimer();
        break;

    case COMMAND_STOP: // STOP TAPE OP
        Stop32kTimer();
        if (tapeMode == TAPE_MODE_SAVE)
        {
            EP6BCH = 0;
            SYNCDELAY;
            EP6BCL = tapeCounter;
            SYNCDELAY;
        }
        else
        {
            while (!(EP2468STAT & bmEP4EMPTY))
            {
                EP4BCL = 0x01; // clear state
                SYNCDELAY;
            }
        }
        StartKeyInt();
        break;

    case COMMAND_FORCE_STOP:
        StopKeyInt();
        IOA = (IOA & 0x0f) | 0x80;
    }
    EP1OUTBC = 0x01;
    SYNCDELAY;
}

void main()
{
    uint8_t i;

    keyPos = 0;
    highValue = 0;

    for (i = 0; i < 10; i++)
    {
        keybufLow[i] = 0;
        keybufHigh[i] = 0;
    }

    Initialize();

    IE = 0x87; // Enable global interrupt, Enable INT0, INT1

    // start GPIF (selection polling)
    XGPIFSGLDATLX = 0x01;

    for (;;)
    {
        if (!(EP01STAT & 2))
        {
            ProcessUsbCommand();
        }
    }
}
