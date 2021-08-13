#include <TimerOne.h>
#include <SdFat.h>

//TZX block list - uncomment as supported
#define ID10                0x10    //Standard speed data block
#define ID11                0x11    //Turbo speed data block
#define ID12                0x12    //Pure tone
#define ID13                0x13    //Sequence of pulses of various lengths
#define ID14                0x14    //Pure data block
//#define ID15                0x15    //Direct recording block -- TBD - curious to load OTLA files using direct recording (22KHz)
//#define ID18                0x18    //CSW recording block
//#define ID19                0x19    //Generalized data block
#define ID20                0x20    //Pause (silence) ot 'Stop the tape' command
#define ID21                0x21    //Group start
#define ID22                0x22    //Group end
#define ID23                0x23    //Jump to block
#define ID24                0x24    //Loop start
#define ID25                0x25    //Loop end
#define ID26                0x26    //Call sequence
#define ID27                0x27    //Return from sequence
#define ID28                0x28    //Select block
#define ID2A                0x2A    //Stop the tape is in 48K mode
#define ID2B                0x2B    //Set signal level
#define ID30                0x30    //Text description
#define ID31                0x31    //Message block
#define ID32                0x32    //Archive info
#define ID33                0x33    //Hardware type
#define ID35                0x35    //Custom info block
#define ID4B                0x4B    //Kansas City block (MSX/BBC/Acorn/...)
#define IDPAUSE				0x59    //Custom Pause processing
#define ID5A                0x5A    //Glue block (90 dec, ASCII Letter 'Z')
#define AYO                 0xFB    //AY file
#define ZXO                 0xFC    //ZX80 O file
#define ZXP                 0xFD    //ZX81 P File
#define TAP                 0xFE    //Tap File Mode
#define EOF                 0xFF    //End of file

//TZX File Tasks
#define GETFILEHEADER         0
#define GETID                 1
#define PROCESSID             2
#define GETAYHEADER           3

//TZX ID Tasks
#define READPARAM             0
#define PILOT                 1
#define SYNC1                 2
#define SYNC2                 3
#define DATA                  4
#define PAUSE                 5

//Buffer size
#define buffsize              64

//Spectrum Standards
#define PILOTLENGTH           619
#define SYNCFIRST             191
#define SYNCSECOND            210
#define ZEROPULSE             244
#define ONEPULSE              489
#define PILOTNUMBERL          8063
#define PILOTNUMBERH          3223
#define PAUSELENGTH           1000

//ZX81 Standards
#define ZX80PULSE                 160
#define ZX80BITGAP                1442

//ZX81 Pulse Patterns - Zero Bit  - HIGH, LOW, HIGH, LOW, HIGH, LOW, HIGH, GAP
//                    - One Bit   - HIGH, LOW, HIGH, LOW, HIGH, LOW, HIGH, LOW, HIGH, LOW, HIGH, LOW, HIGH, LOW, HIGH, LOW, HIGH, GAP

// AY Header offset start
#define HDRSTART              0

//Keep track of which ID, Task, and Block Task we're dealing with
byte currentID = 0;
byte currentTask = 0;
byte currentBlockTask = 0;

//Temporarily store for a pulse period before loading it into the buffer.
word currentPeriod=1;

//ISR Variables
volatile byte pos = 0;
volatile word wbuffer[buffsize+1][2];
volatile byte morebuff = HIGH;
volatile byte workingBuffer=0;
volatile byte isStopped=false;
volatile byte pinState=LOW;
volatile byte isPauseBlock = false;
volatile byte wasPauseBlock = false;
volatile byte intError = false;

//Main Variables
byte AYPASS = 0;
byte hdrptr = 0;
byte blkchksum = 0;
word ayblklen = 0;
byte btemppos = 0;
byte copybuff = LOW;
unsigned long bytesRead=0;
unsigned long bytesToRead=0;
byte pulsesCountByte=0;
word pilotPulses=0;
word pilotLength=0;
word sync1Length=0;
word sync2Length=0;
word zeroPulse=0;
word onePulse=0;
word TstatesperSample=0;
byte usedBitsInLastByte=8;
word loopCount=0;
byte seqPulses=0;
unsigned long loopStart=0;
word pauseLength=0;
word temppause=0;
byte outByte=0;
word outWord=0;
unsigned long outLong=0;
byte count=128;
volatile byte currentBit=0;
volatile byte currentByte=0;
volatile byte currentChar=0;
byte pass=0;
unsigned long debugCount=0;
byte EndOfFile=false;

int TSXspeedup = 1;
int BAUDRATE = 1200;

#define STR_STOPPED_FULL    F("Stopped         ")
#define STR_ERR_OPEN_FILE   F("Err: Open File  ")
#define STR_ERR_ID          F("Err: Unknown ID ")
#define STR_ERR_NOTTZX      F("Err: Not TZXFile")
#define STR_ERR_NOTAY       F("Err: Not AY File")
#define STR_ERR_READING     F("Err: Read File  ")

const char TZXTape[] PROGMEM = {'Z','X','T','a','p','e','!'};
const char ZX81Filename[] PROGMEM = {'T','Z','X','D','U','I','N','O',0x9D};
const char AYFile[] PROGMEM = {'Z','X','A','Y','E','M','U','L'};           // added additional AY file header check
const char TAPHdr[] PROGMEM = {0x0,0x0,0x3,'Z','X','A','Y','F','i','l','e',' ',' ',0x1A,0xB,0x0,0xC0,0x0,0x80,0x6E};

SdFile entry;

size_t printLine(const __FlashStringHelper *sz, uint8_t row);
bool isFileStopped();
void stopFile();
void sound(uint8_t val);

int ReadByte(unsigned long pos);
int ReadWord(unsigned long pos);
int ReadLong(unsigned long pos);
int ReadDword(unsigned long pos);
void checkForEXT(char *filename);
void TZXProcess();
void wave();
void StandardBlock();
void writeData();
void writeData4B();
void writeHeader();
void ZX81FilenameBlock();
void ZX8081DataBlock();
void ZX80ByteWrite();
void PureToneBlock();
void PureDataBlock();
void PulseSequenceBlock();
void ReadTZXHeader();
void ReadAYHeader();

void clearBuffer()
{
   for (int i = 0; i <= buffsize; i++)
    {
        wbuffer[i][0] = 0;
        wbuffer[i][1] = 0;
    }
}

word TickToUs(word ticks)
{
    return (word)((((float)ticks) / 3.5) + 0.5);
}

void TZXPlay(char *filename)
{
    Timer1.stop(); //Stop timer interrupt
    if (!entry.open(filename, O_READ))
    { //open file and check for errors
        printLine(STR_ERR_OPEN_FILE, 0);
    }
    bytesRead = 0;               //start of file
    ayblklen = entry.fileSize() + 3; // add 3 file header, data byte and chksum byte to file length
    currentTask = GETFILEHEADER; //First task: search for header
    checkForEXT(filename);
    currentBlockTask = READPARAM; //First block task is to read in parameters
    clearBuffer();
    isStopped = false;
    pinState = LOW; //Always Start on a LOW output for simplicity
    count = 255;    //End of file buffer flush
    EndOfFile = false;
    sound(pinState);
    Timer1.setPeriod(1000); //set 1ms wait at start of a file.
}

bool checkForTap(char *filename)
{
    //Check for TAP file extensions as these have no header
    byte len = strlen(filename);
    if (strstr_P(strlwr(filename + (len - 4)), PSTR(".tap")))
    {
        return true;
    }
    return false;
}

bool checkForP(char *filename)
{
    //Check for TAP file extensions as these have no header
    byte len = strlen(filename);
    if (strstr_P(strlwr(filename + (len - 2)), PSTR(".p")))
    {
        return true;
    }
    return false;
}

bool checkForO(char *filename)
{
    //Check for TAP file extensions as these have no header
    byte len = strlen(filename);
    if (strstr_P(strlwr(filename + (len - 2)), PSTR(".o")))
    {
        return true;
    }
    return false;
}

bool checkForAY(char *filename)
{
    //Check for AY file extensions as these have no header
    byte len = strlen(filename);
    if (strstr_P(strlwr(filename + (len - 3)), PSTR(".ay")))
    {
        return true;
    }
    return false;
}

void checkForEXT(char *filename)
{
    if (checkForTap(filename))
    { //Check for Tap File.  As these have no header we can skip straight to playing data
        currentTask = PROCESSID;
        currentID = TAP;
    }
    if (checkForP(filename))
    { //Check for P File.  As these have no header we can skip straight to playing data
        currentTask = PROCESSID;
        currentID = ZXP;
    }
    if (checkForO(filename))
    { //Check for O File.  As these have no header we can skip straight to playing data
        currentTask = PROCESSID;
        currentID = ZXO;
    }
    if (checkForAY(filename))
    { //Check for AY File.  As these have no TAP header we must create it and send AY DATA Block after
        currentTask = GETAYHEADER;
        currentID = AYO;
        AYPASS = 0;        // Reset AY PASS flags
        hdrptr = HDRSTART; // Start reading from position 1 -> 0x13 [0x00]
    }
}

void TZXStop()
{
    Timer1.stop(); //Stop timer
    isStopped = true;
    entry.close(); //Close file
                   // DEBUGGING Stuff

    bytesRead = 0; // reset read bytes PlayBytes
    blkchksum = 0; // reset block chksum byte for AY loading routine
    AYPASS = 0;    // reset AY flag
}

void TZXLoop()
{
    noInterrupts(); //Pause interrupts to prevent var reads and copy values out
    copybuff = morebuff;
    morebuff = LOW;
    isStopped = isFileStopped();
    interrupts();
    if (copybuff == HIGH)
    {
        btemppos = 0; //Buffer has swapped, start from the beginning of the new page
        copybuff = LOW;
    }

    if (btemppos <= buffsize) // Keep filling until full
    {
        TZXProcess(); //generate the next period to add to the buffer
        if (currentPeriod > 0)
        {
            noInterrupts();                                       //Pause interrupts while we add a period to the buffer
            wbuffer[btemppos][workingBuffer ^ 1] = currentPeriod; //add period to the buffer
            interrupts();
            btemppos += 1;
        }
    }
}

void TZXSetup()
{
    isStopped = true;
    pinState = LOW;
    Timer1.initialize(100000); //100ms pause prevents anything bad happening before we're ready
    Timer1.attachInterrupt(wave);
    Timer1.stop(); //Stop the timer until we're ready
}

void TZXProcess()
{
    byte r = 0;
    currentPeriod = 0;
    if (currentTask == GETFILEHEADER)
    {
        //grab 7 byte string
        ReadTZXHeader();
        //set current task to GETID
        currentTask = GETID;
    }
    if (currentTask == GETAYHEADER)
    {
        //grab 8 byte string
        ReadAYHeader();
        //set current task to PROCESSID
        currentTask = PROCESSID;
    }
    if (currentTask == GETID)
    {
        //grab 1 byte ID
        if (ReadByte(bytesRead) == 1)
        {
            currentID = outByte;
        }
        else
        {
            currentID = EOF;
        }
        //reset data block values
        currentBit = 0;
        pass = 0;
        //set current task to PROCESSID
        currentTask = PROCESSID;
        currentBlockTask = READPARAM;
    }
    if (currentTask == PROCESSID)
    {
        //ID Processing
        switch (currentID)
        {
        case ID10:
            //Process ID10 - Standard Block
            switch (currentBlockTask)
            {
            case READPARAM:
                if (r = ReadWord(bytesRead) == 2)
                {
                    pauseLength = outWord;
                }
                if (r = ReadWord(bytesRead) == 2)
                {
                    bytesToRead = outWord + 1;
                }
                if (r = ReadByte(bytesRead) == 1)
                {
                    if (outByte == 0)
                    {
                        pilotPulses = PILOTNUMBERL;
                    }
                    else
                    {
                        pilotPulses = PILOTNUMBERH;
                    }
                    bytesRead += -1;
                }
                pilotLength = PILOTLENGTH;
                sync1Length = SYNCFIRST;
                sync2Length = SYNCSECOND;
                zeroPulse = ZEROPULSE;
                onePulse = ONEPULSE;
                currentBlockTask = PILOT;
                usedBitsInLastByte = 8;
                break;

            default:
                StandardBlock();
                break;
            }

            break;

        case ID11:
            //Process ID11 - Turbo Tape Block
            switch (currentBlockTask)
            {
            case READPARAM:
                if (r = ReadWord(bytesRead) == 2)
                {
                    pilotLength = TickToUs(outWord);
                }
                if (r = ReadWord(bytesRead) == 2)
                {
                    sync1Length = TickToUs(outWord);
                }
                if (r = ReadWord(bytesRead) == 2)
                {
                    sync2Length = TickToUs(outWord);
                }
                if (r = ReadWord(bytesRead) == 2)
                {
                    zeroPulse = TickToUs(outWord);
                }
                if (r = ReadWord(bytesRead) == 2)
                {
                    onePulse = TickToUs(outWord);
                }
                if (r = ReadWord(bytesRead) == 2)
                {
                    pilotPulses = outWord;
                }
                if (r = ReadByte(bytesRead) == 1)
                {
                    usedBitsInLastByte = outByte;
                }
                if (r = ReadWord(bytesRead) == 2)
                {
                    pauseLength = outWord;
                }
                if (r = ReadLong(bytesRead) == 3)
                {
                    bytesToRead = outLong + 1;
                }
                currentBlockTask = PILOT;
                break;

            default:
                StandardBlock();
                break;
            }

            break;
        case ID12:
            //Process ID12 - Pure Tone Block
            if (currentBlockTask == READPARAM)
            {
                if (r = ReadWord(bytesRead) == 2)
                {
                    pilotLength = TickToUs(outWord);
                }
                if (r = ReadWord(bytesRead) == 2)
                {
                    pilotPulses = outWord;
                    //DebugBlock("Pilot Pulses", pilotPulses);
                }
                currentBlockTask = PILOT;
            }
            else
            {
                PureToneBlock();
            }
            break;

        case ID13:
            //Process ID13 - Sequence of Pulses
            if (currentBlockTask == READPARAM)
            {
                if (r = ReadByte(bytesRead) == 1)
                {
                    seqPulses = outByte;
                }
                currentBlockTask = DATA;
            }
            else
            {
                PulseSequenceBlock();
            }
            break;

        case ID14:
            //process ID14 - Pure Data Block
            if (currentBlockTask == READPARAM)
            {
                if (r = ReadWord(bytesRead) == 2)
                {
                    zeroPulse = TickToUs(outWord);
                }
                if (r = ReadWord(bytesRead) == 2)
                {
                    onePulse = TickToUs(outWord);
                }
                if (r = ReadByte(bytesRead) == 1)
                {
                    usedBitsInLastByte = outByte;
                }
                if (r = ReadWord(bytesRead) == 2)
                {
                    pauseLength = outWord;
                }
                if (r = ReadLong(bytesRead) == 3)
                {
                    bytesToRead = outLong + 1;
                }
                currentBlockTask = DATA;
            }
            else
            {
                PureDataBlock();
            }
            break;
            /*
        case ID15:
          //process ID15 - Direct Recording
          if(currentBlockTask==READPARAM) {
            if(r=ReadWord(bytesRead)==2) {
              //Number of T-states per sample (bit of data) 79 or 158 - 22.6757uS for 44.1KHz
              TstatesperSample = TickToUs(outWord);
            }
            if(r=ReadWord(bytesRead)==2) {
              //Pause after this block in milliseconds
              pauseLength = outWord;
            }
            if(r=ReadByte(bytesRead)==1) {
            //Used bits in last byte (other bits should be 0)
              usedBitsInLastByte = outByte;
            }
            if(r=ReadLong(bytesRead)==3) {
              // Length of samples' data
              bytesToRead = outLong+1;
            }
            currentBlockTask=DATA;
          } else {
            DirectRecording();
          }
        break;
        */

            /*  //Old ID20
        case ID20:
          //process ID20 - Pause Block
          if(r=ReadWord(bytesRead)==2) {
            if(outWord>0) {
              currentPeriod = pauseLength;
              bitSet(currentPeriod, 15);
            }
            currentTask=GETID;
          }
        break; */

        case ID20:
            //process ID20 - Pause Block
            if (r = ReadWord(bytesRead) == 2)
            {
                if (outWord > 0)
                {
                    temppause = outWord;
                    currentID = IDPAUSE;
                }
                else
                {
                    currentTask = GETID;
                }
            }
            break;

        case ID21:
            //Process ID21 - Group Start
            if (r = ReadByte(bytesRead) == 1)
            {
                bytesRead += outByte;
            }
            currentTask = GETID;
            break;

        case ID22:
            //Process ID22 - Group End
            currentTask = GETID;
            break;

        case ID24:
            //Process ID24 - Loop Start
            if (r = ReadWord(bytesRead) == 2)
            {
                loopCount = outWord;
                loopStart = bytesRead;
            }
            currentTask = GETID;
            break;

        case ID25:
            //Process ID25 - Loop End
            loopCount += -1;
            if (loopCount != 0)
            {
                bytesRead = loopStart;
            }
            currentTask = GETID;
            break;

        case ID2A:
            //Skip//
            bytesRead += 4;
            currentTask = GETID;
            break;

        case ID2B:
            //Skip//
            bytesRead += 5;
            currentTask = GETID;
            break;

        case ID30:
            //Process ID30 - Text Description
            if (r = ReadByte(bytesRead) == 1)
            {
                bytesRead += outByte;
            }
            currentTask = GETID;
            break;

        case ID31:
            //Process ID31 - Message block
            if (r = ReadByte(bytesRead) == 1)
            {
                // dispayTime = outByte;
            }
            if (r = ReadByte(bytesRead) == 1)
            {
                bytesRead += outByte;
            }
            currentTask = GETID;
            break;

        case ID32:
            //Process ID32 - Archive Info
            //Block Skipped until larger screen used
            if (ReadWord(bytesRead) == 2)
            {
                bytesRead += outWord;
            }
            currentTask = GETID;
            break;

        case ID33:
            //Process ID32 - Archive Info
            //Block Skipped until larger screen used
            if (ReadByte(bytesRead) == 1)
            {
                bytesRead += (long(outByte) * 3);
            }
            currentTask = GETID;
            break;

        case ID35:
            //Process ID35 - Custom Info Block
            //Block Skipped
            bytesRead += 0x10;
            if (r = ReadDword(bytesRead) == 4)
            {
                bytesRead += outLong;
            }
            currentTask = GETID;
            break;

        case ID4B:
            //Process ID4B - Kansas City Block (MSX specific implementation only)
            switch (currentBlockTask)
            {
            case READPARAM:
                if (r = ReadDword(bytesRead) == 4)
                { // Data size to read
                    bytesToRead = outLong - 12;
                }
                if (r = ReadWord(bytesRead) == 2)
                { // Pause after block in ms
                    pauseLength = outWord;
                }
                if (TSXspeedup == 0)
                {
                    if (r = ReadWord(bytesRead) == 2)
                    { // T-states each pilot pulse
                        pilotLength = TickToUs(outWord);
                    }
                    if (r = ReadWord(bytesRead) == 2)
                    { // Number of pilot pulses
                        pilotPulses = outWord;
                    }
                    if (r = ReadWord(bytesRead) == 2)
                    { // T-states 0 bit pulse
                        zeroPulse = TickToUs(outWord);
                    }
                    if (r = ReadWord(bytesRead) == 2)
                    { // T-states 1 bit pulse
                        onePulse = TickToUs(outWord);
                    }
                    ReadWord(bytesRead);
                }
                else
                {
                    //Fixed speedup baudrate, reduced pilot duration
                    pilotPulses = 10000;
                    bytesRead += 10;
                    switch (BAUDRATE)
                    {
                    case 1200:
                        pilotLength = onePulse = TickToUs(729);
                        zeroPulse = TickToUs(1458);
                        break;
                    case 2400:
                        pilotLength = onePulse = TickToUs(365);
                        zeroPulse = TickToUs(729);
                        break;
                    case 3200:
                        pilotLength = onePulse = TickToUs(273);
                        zeroPulse = TickToUs(546);
                        break;
                    case 3600:
                        pilotLength = onePulse = TickToUs(243);
                        zeroPulse = TickToUs(486);
                        break;
                    case 3675:
                        pilotLength = onePulse = TickToUs(236);
                        zeroPulse = TickToUs(472);
                        break;
                    }

                } //TSX_SPEEDUP

                /*              if(r=ReadByte(bytesRead)==1) {  // BitCfg
// No needed if only MSX is supported
                oneBitPulses =  outByte & 0x0f;       //(default:4)
                zeroBitPulses = outByte >> 4;         //(default:2)
                if (!oneBitPulses) oneBitPulses = 16;
                if (!zeroBitPulses) zeroBitPulses = 16;
              }
              if(r=ReadByte(bytesRead)==1) {  // ByteCfg
// No needed if only MSX is supported
                startBits = (outByte >> 6) & 3;       //(default:1)
                startBitValue = (outByte >> 5) & 1;   //(default:0)
                stopBits = (outByte >> 3) & 3;        //(default:2)
                stopBitValue = (outByte >> 2) & 1;    //(default:1)
                endianness = outByte & 1;             //0:LSb 1:MSb (default:0)
              }*/
                currentBlockTask = PILOT;
                break;

            case PILOT:
                //Start with Pilot Pulses
                if (!pilotPulses--)
                {
                    currentBlockTask = DATA;
                }
                else
                {
                    currentPeriod = pilotLength;
                }
                break;

            case DATA:
                //Data playback
                writeData4B();
                break;

            case PAUSE:
                //Close block with a pause
                temppause = pauseLength;
                currentID = IDPAUSE;
                break;
            }
            break;

        case TAP:
            //Pure Tap file block
            switch (currentBlockTask)
            {
            case READPARAM:
                pauseLength = PAUSELENGTH;
                if (r = ReadWord(bytesRead) == 2)
                {
                    bytesToRead = outWord + 1;
                }
                if (r = ReadByte(bytesRead) == 1)
                {
                    if (outByte == 0)
                    {
                        pilotPulses = PILOTNUMBERL + 1;
                    }
                    else
                    {
                        pilotPulses = PILOTNUMBERH + 1;
                    }
                    bytesRead += -1;
                }
                pilotLength = PILOTLENGTH;
                sync1Length = SYNCFIRST;
                sync2Length = SYNCSECOND;
                zeroPulse = ZEROPULSE;
                onePulse = ONEPULSE;
                currentBlockTask = PILOT;
                usedBitsInLastByte = 8;
                break;

            default:
                StandardBlock();
                break;
            }
            break;

        case ZXP:
            switch (currentBlockTask)
            {
            case READPARAM:
                pauseLength = PAUSELENGTH * 5;
                currentChar = 0;
                currentBlockTask = PILOT;
                break;

            case PILOT:
                ZX81FilenameBlock();
                break;

            case DATA:
                ZX8081DataBlock();
                break;
            }
            break;

        case ZXO:
            switch (currentBlockTask)
            {
            case READPARAM:
                pauseLength = PAUSELENGTH * 5;
                currentBlockTask = DATA;
                break;

            case DATA:
                ZX8081DataBlock();
                break;
            }
            break;

        case AYO: //AY File - Pure AY file block - no header, must emulate it
            switch (currentBlockTask)
            {
            case READPARAM:
                pauseLength = PAUSELENGTH; // Standard 1 sec pause
                                           // here we must generate the TAP header which in pure AY files is missing.
                                           // This was done with a DOS utility called FILE2TAP which does not work under recent 32bit OSs (only using DOSBOX).
                                           // TAPed AY files begin with a standard 0x13 0x00 header (0x13 bytes to follow) and contain the
                                           // name of the AY file (max 10 bytes) which we will display as "ZXAYFile " followed by the
                                           // length of the block (word), checksum plus 0xFF to indicate next block is DATA.
                                           // 13 00[00 03(5A 58 41 59 46 49 4C 45 2E 49)1A 0B 00 C0 00 80]21<->[1C 0B FF<AYFILE>CHK]
                //if(hdrptr==1) {
                //bytesToRead = 0x13-2; // 0x13 0x0 - TAP Header minus 2 (FLAG and CHKSUM bytes) 17 bytes total
                //}
                if (hdrptr == HDRSTART)
                {
                    //if (!AYPASS) {
                    pilotPulses = PILOTNUMBERL + 1;
                }
                else
                {
                    pilotPulses = PILOTNUMBERH + 1;
                }
                pilotLength = PILOTLENGTH;
                sync1Length = SYNCFIRST;
                sync2Length = SYNCSECOND;
                zeroPulse = ZEROPULSE;
                onePulse = ONEPULSE;
                currentBlockTask = PILOT; // now send pilot, SYNC1, SYNC2 and DATA (writeheader() from String Vector on 1st pass then writeData() on second)
                if (hdrptr == HDRSTART)
                    AYPASS = 1; // Set AY TAP data read flag only if first run
                if (AYPASS == 2)
                { // If we have already sent TAP header
                    blkchksum = 0;
                    bytesRead = 0;
                    bytesToRead = ayblklen + 2; // set length of file to be read plus data byte and CHKSUM (and 2 block LEN bytes)
                    AYPASS = 5;                 // reset flag to read from file and output header 0xFF byte and end chksum
                }
                usedBitsInLastByte = 8;
                break;

            default:
                StandardBlock();
                break;
            }
            break;

        case IDPAUSE:
            /*     currentPeriod = temppause;
              temppause = 0;
              currentTask = GETID;
              bitSet(currentPeriod, 15);       */
            if (temppause > 0)
            {
                if (temppause > 8300)
                {
                    //Serial.println(temppause, DEC);
                    currentPeriod = 8300;
                    temppause += -8300;
                }
                else
                {
                    currentPeriod = temppause;
                    temppause = 0;
                }
                bitSet(currentPeriod, 15);
            }
            else
            {
                currentTask = GETID;
                if (EndOfFile == true)
                    currentID = EOF;
            }
            break;

        case EOF:
            //Handle end of file
            if (!count == 0)
            {
                currentPeriod = 32767;
                count += -1;
            }
            else
            {
                stopFile();
                return;
            }
            break;

        default:
            //ID Not Recognised - Fall back if non TZX file or unrecognised ID occurs
            printLine(STR_ERR_ID, 1);
            delay(5000);
            stopFile();
            break;
        }
    }
}

void StandardBlock()
{
    //Standard Block Playback
    switch (currentBlockTask)
    {
    case PILOT:
        //Start with Pilot Pulses
        currentPeriod = pilotLength;
        pilotPulses += -1;
        if (pilotPulses == 0)
        {
            currentBlockTask = SYNC1;
        }
        break;

    case SYNC1:
        //First Sync Pulse
        currentPeriod = sync1Length;
        currentBlockTask = SYNC2;
        break;

    case SYNC2:
        //Second Sync Pulse
        currentPeriod = sync2Length;
        currentBlockTask = DATA;
        break;

    case DATA:
        //Data Playback
        if ((AYPASS == 0) | (AYPASS == 4) | (AYPASS == 5))
            writeData(); // Check if we are playing from file or Vector String and we need to send first 0xFF byte or checksum byte at EOF
        else
        {
            writeHeader(); // write TAP Header data from String Vector (AYPASS=1)
        }
        break;

    case PAUSE:
        //Close block with a pause

        if ((currentID != TAP) && (currentID != AYO))
        { // Check if we have !=AYO too
            temppause = pauseLength;
            currentID = IDPAUSE;
        }
        else
        {
            currentPeriod = pauseLength;
            bitSet(currentPeriod, 15);
            currentBlockTask = READPARAM;
        }
        if (EndOfFile == true)
            currentID = EOF;
        break;
    }
}

void PureToneBlock()
{
    //Pure Tone Block - Long string of pulses with the same length
    currentPeriod = pilotLength;
    pilotPulses += -1;
    if (pilotPulses == 0)
    {
        currentTask = GETID;
    }
}

void PulseSequenceBlock()
{
    //Pulse Sequence Block - String of pulses each with a different length
    //Mainly used in speedload blocks
    byte r = 0;
    if (r = ReadWord(bytesRead) == 2)
    {
        currentPeriod = TickToUs(outWord);
    }
    seqPulses += -1;
    if (seqPulses == 0)
    {
        currentTask = GETID;
    }
}

void PureDataBlock()
{
    //Pure Data Block - Data & pause only, no header, sync
    switch (currentBlockTask)
    {
    case DATA:
        writeData();
        break;

    case PAUSE:
        temppause = pauseLength;
        currentID = IDPAUSE;
        break;
    }
}

/*
void KCSBlock() {
  //Kansas City Standard Block Playback (MSX specific)
  switch(currentBlockTask) {

    case PILOT:
        //Start with Pilot Pulses
        if (!pilotPulses--) {
          currentBlockTask = DATA;
        } else {
          currentPeriod = pilotLength;
        }
    break;

    case DATA:
        //Data playback
        writeData4B();
    break;

    case PAUSE:
        //Close block with a pause
        temppause = pauseLength;
        currentID = IDPAUSE;
    break;

  }
}
*/

void writeData4B()
{
    //Convert byte (4B Block) from file into string of pulses.  One pulse per pass
    byte r;
    byte dataBit;

    //Continue with current byte
    if (currentBit > 0)
    {

        //Start bit (0)
        if (currentBit == 11)
        {
            currentPeriod = zeroPulse;
            pass += 1;
            if (pass == 2)
            {
                currentBit += -1;
                pass = 0;
            }
        }
        else
            //Stop bits (1)
            if (currentBit <= 2)
        {
            currentPeriod = onePulse;
            pass += 1;
            if (pass == 4)
            {
                currentBit += -1;
                pass = 0;
            }
        }
        else
        //Data bits
        {
            dataBit = currentByte & 1;
            currentPeriod = dataBit == 1 ? onePulse : zeroPulse;
            pass += 1;
            if ((dataBit == 1 && pass == 4) || (dataBit == 0 && pass == 2))
            {
                currentByte >>= 1;
                currentBit += -1;
                pass = 0;
            }
        }
    }
    else if (currentBit == 0 && bytesToRead != 0)
    {
        //Read new byte
        if (r = ReadByte(bytesRead) == 1)
        {
            bytesToRead += -1;
            currentByte = outByte;
            currentBit = 11;
            pass = 0;
        }
        else if (r == 0)
        {
            //End of file
            currentID = EOF;
            return;
        }
    }

    //End of block?
    if (bytesToRead == 0 && currentBit == 0)
    {
        temppause = pauseLength;
        currentBlockTask = PAUSE;
    }
}

void DirectRecording()
{
    //Direct Recording - Output bits based on specified sample rate (Ticks per clock) either 44.1KHz or 22.05
    switch (currentBlockTask)
    {
    case DATA:
        writeData();
        break;

    case PAUSE:
        temppause = pauseLength;
        currentID = IDPAUSE;
        break;
    }
}

void ZX81FilenameBlock()
{
    //output ZX81 filename data  byte r;
    if (currentBit == 0)
    { //Check for byte end/first byte
        //currentByte=ZX81Filename[currentChar];
        currentByte = pgm_read_byte(ZX81Filename + currentChar);
        currentChar += 1;
        if (currentChar == 10)
        {
            currentBlockTask = DATA;
            return;
        }
        currentBit = 9;
        pass = 0;
    }
    /*currentPeriod = ZX80PULSE;
  if(pass==1) {
    currentPeriod=ZX80BITGAP;
  }
  if(pass==0) {
    if(currentByte&0x80) {                       //Set next period depending on value of bit 0
      pass=19;
    } else {
      pass=9;
    }
    currentByte <<= 1;                        //Shift along to the next bit
    currentBit += -1;
    currentPeriod=0;
  }
  pass+=-1;*/
    ZX80ByteWrite();
}

void ZX8081DataBlock()
{
    byte r;
    if (currentBit == 0)
    { //Check for byte end/first byte
        if (r = ReadByte(bytesRead) == 1)
        { //Read in a byte
            currentByte = outByte;
            bytesToRead += -1;
            /*if(bytesToRead == 0) {                  //Check for end of data block
        bytesRead += -1;                      //rewind a byte if we've reached the end
        if(pauseLength==0) {                  //Search for next ID if there is no pause
          currentTask = GETID;
        } else {
          currentBlockTask = PAUSE;           //Otherwise start the pause
        }
        return;
      }*/
        }
        else if (r == 0)
        {
            EndOfFile = true;
            temppause = pauseLength;
            currentID = IDPAUSE;
            return;
        }
        currentBit = 9;
        pass = 0;
    }
    /*currentPeriod = ZX80PULSE;
  if(pass==1) {
    currentPeriod=ZX80BITGAP;
  }
  if(pass==0) {
    if(currentByte&0x80) {                       //Set next period depending on value of bit 0
      pass=19;
    } else {
      pass=9;
    }
    currentByte <<= 1;                        //Shift along to the next bit
    currentBit += -1;
    currentPeriod=0;
  }
  pass+=-1;*/
    ZX80ByteWrite();
}

void ZX80ByteWrite()
{
    currentPeriod = ZX80PULSE;
    if (pass == 1)
    {
        currentPeriod = ZX80BITGAP;
    }
    if (pass == 0)
    {
        if (currentByte & 0x80)
        { //Set next period depending on value of bit 0
            pass = 19;
        }
        else
        {
            pass = 9;
        }
        currentByte <<= 1; //Shift along to the next bit
        currentBit += -1;
        currentPeriod = 0;
    }
    pass += -1;
}

void writeData()
{
    //Convert byte from file into string of pulses.  One pulse per pass
    byte r;
    if (currentBit == 0)
    { //Check for byte end/first byte
        if (r = ReadByte(bytesRead) == 1)
        { //Read in a byte
            currentByte = outByte;
            if (AYPASS == 5)
            {
                currentByte = 0xFF; // Only insert first DATA byte if sending AY TAP DATA Block and don't decrement counter
                AYPASS = 4;         // set Checksum flag to be sent when EOF reached
                bytesRead += -1;    // rollback ptr and compensate for dummy read byte
                bytesToRead += 2;   // add 2 bytes to read as we send 0xFF (data flag header byte) and chksum at the end of the block
            }
            else
            {
                bytesToRead += -1;
            }
            blkchksum = blkchksum ^ currentByte; // keep calculating checksum
            if (bytesToRead == 0)
            {                    //Check for end of data block
                bytesRead += -1; //rewind a byte if we've reached the end
                if (pauseLength == 0)
                { //Search for next ID if there is no pause
                    currentTask = GETID;
                }
                else
                {
                    currentBlockTask = PAUSE; //Otherwise start the pause
                }
                return; // exit
            }
        }
        else if (r == 0)
        { // If we reached the EOF
            if (AYPASS != 4)
            { // Check if need to send checksum
                EndOfFile = true;
                if (pauseLength == 0)
                {
                    currentTask = GETID;
                }
                else
                {
                    currentBlockTask = PAUSE;
                }
                return; // return here if normal TAP or TZX
            }
            else
            {
                currentByte = blkchksum; // else send calculated chksum
                bytesToRead += 1;        // add one byte to read
                AYPASS = 0;              // Reset flag to end block
            }
            //return;
        }
        if (bytesToRead != 1)
        { //If we're not reading the last byte play all 8 bits
            currentBit = 8;
        }
        else
        {
            currentBit = usedBitsInLastByte; //Otherwise only play back the bits needed
        }
        pass = 0;
    }
    if (currentByte & 0x80)
    { //Set next period depending on value of bit 0
        currentPeriod = onePulse;
    }
    else
    {
        currentPeriod = zeroPulse;
    }
    pass += 1; //Data is played as 2 x pulses
    if (pass == 2)
    {
        currentByte <<= 1; //Shift along to the next bit
        currentBit += -1;
        pass = 0;
    }
}

void writeHeader()
{
    //Convert byte from HDR Vector String into string of pulses and calculate checksum. One pulse per pass
    if (currentBit == 0)
    { //Check for byte end/new byte
        if (hdrptr == 19)
        { // If we've reached end of header block send checksum byte
            currentByte = blkchksum;
            AYPASS = 2;               // set flag to Stop playing from header in RAM
            currentBlockTask = PAUSE; // we've finished outputting the TAP header so now PAUSE and send DATA block normally from file
            return;
        }
        hdrptr += 1; // increase header string vector pointer
        if (hdrptr < 20)
        { //Read a byte until we reach end of tap header
            //currentByte = TAPHdr[hdrptr];
            currentByte = pgm_read_byte(TAPHdr + hdrptr);
            if (hdrptr == 13)
            { // insert calculated block length minus LEN bytes
                currentByte = lowByte(ayblklen - 3);
            }
            else if (hdrptr == 14)
            {
                currentByte = highByte(ayblklen);
            }
            blkchksum = blkchksum ^ currentByte; // Keep track of Chksum
                                                 //}
                                                 //if(hdrptr<20) {               //If we're not reading the last byte play all 8 bits
                                                 //if(bytesToRead!=1) {                      //If we're not reading the last byte play all 8 bits
            currentBit = 8;
        }
        else
        {
            currentBit = usedBitsInLastByte; //Otherwise only play back the bits needed
        }
        pass = 0;
    } //End if currentBit == 0
    if (currentByte & 0x80)
    { //Set next period depending on value of bit 0
        currentPeriod = onePulse;
    }
    else
    {
        currentPeriod = zeroPulse;
    }
    pass += 1; //Data is played as 2 x pulses
    if (pass == 2)
    {
        currentByte <<= 1; //Shift along to the next bit
        currentBit += -1;
        pass = 0;
    }
} // End writeHeader()

void wave()
{
    //ISR Output routine
    //unsigned long fudgeTime = micros();         //fudgeTime is used to reduce length of the next period by the time taken to process the ISR
    word workingPeriod = wbuffer[pos][workingBuffer];
    byte pauseFlipBit = false;
    unsigned long newTime = 1;
    intError = false;
    if (isStopped == 0 && workingPeriod >= 1)
    {
        if (bitRead(workingPeriod, 15))
        {
            //If bit 15 of the current period is set we're about to run a pause
            //Pauses start with a 1.5ms where the output is untouched after which the output is set LOW
            //Pause block periods are stored in milliseconds not microseconds
            isPauseBlock = true;
            bitClear(workingPeriod, 15); //Clear pause block flag
            pinState = !pinState;
            pauseFlipBit = true;
            wasPauseBlock = true;
        }
        else
        {
            if (workingPeriod >= 1 && wasPauseBlock == false)
            {
                pinState = !pinState;
            }
            else if (wasPauseBlock == true && isPauseBlock == false)
            {
                wasPauseBlock = false;
            }
        }
        sound(pinState);
        if (pauseFlipBit == true)
        {
            newTime = 1500;                                  //Set 1.5ms initial pause block
            pinState = LOW;                                  //Set next pinstate LOW
            wbuffer[pos][workingBuffer] = workingPeriod - 1; //reduce pause by 1ms as we've already pause for 1.5ms
            pauseFlipBit = false;
        }
        else
        {
            if (isPauseBlock == true)
            {
                newTime = long(workingPeriod) * 1000; //Set pause length in microseconds
                isPauseBlock = false;
            }
            else
            {
                newTime = workingPeriod; //After all that, if it's not a pause block set the pulse period
            }
            pos += 1;
            if (pos > buffsize) //Swap buffer pages if we've reached the end
            {
                pos = 0;
                workingBuffer ^= 1;
                morebuff = HIGH; //Request more data to fill inactive page
            }
        }
    }
    else if (workingPeriod <= 1 && isStopped == 0)
    {
        newTime = 1000; //Just in case we have a 0 in the buffer
        pos += 1;
        if (pos > buffsize)
        {
            pos = 0;
            workingBuffer ^= 1;
            morebuff = HIGH;
        }
    }
    else
    {
        newTime = 1000000; //Just in case we have a 0 in the buffer
    }
    //newTime += 12;
    //fudgeTime = micros() - fudgeTime;         //Compensate for stupidly long ISR
    //Timer1.setPeriod(newTime - fudgeTime);    //Finally set the next pulse length
    Timer1.setPeriod(newTime + 4); //Finally set the next pulse length
}

int ReadByte(unsigned long pos)
{
    //Read a byte from the file, and move file position on one if successful
    byte out[1];
    int i = 0;
    if (entry.seekSet(pos))
    {
        i = entry.read(out, 1);
        if (i == 1)
            bytesRead += 1;
    }
    outByte = out[0];
    //blkchksum = blkchksum ^ out[0];
    return i;
}

int ReadWord(unsigned long pos)
{
    //Read 2 bytes from the file, and move file position on two if successful
    byte out[2];
    int i = 0;
    if (entry.seekSet(pos))
    {
        i = entry.read(out, 2);
        if (i == 2)
            bytesRead += 2;
    }
    outWord = word(out[1], out[0]);
    //blkchksum = blkchksum ^ out[0] ^ out[1];
    return i;
}

int ReadLong(unsigned long pos)
{
    //Read 3 bytes from the file, and move file position on three if successful
    byte out[3];
    int i = 0;
    if (entry.seekSet(pos))
    {
        i = entry.read(out, 3);
        if (i == 3)
            bytesRead += 3;
    }
    outLong = (word(out[2], out[1]) << 8) | out[0];
    //blkchksum = blkchksum ^ out[0] ^ out[1] ^ out[2];
    return i;
}

int ReadDword(unsigned long pos)
{
    //Read 4 bytes from the file, and move file position on four if successful
    byte out[4];
    int i = 0;
    if (entry.seekSet(pos))
    {
        i = entry.read(out, 4);
        if (i == 4)
            bytesRead += 4;
    }
    outLong = (word(out[3], out[2]) << 16) | word(out[1], out[0]);
    //blkchksum = blkchksum ^ out[0] ^ out[1] ^ out[2] ^ out[3];
    return i;
}

void ReadTZXHeader()
{
    //Read and check first 10 bytes for a TZX header
    char tzxHeader[11];
    int i = 0;

    if (entry.seekSet(0))
    {
        i = entry.read(tzxHeader, 10);
        if (memcmp_P(tzxHeader, TZXTape, 7) != 0)
        {
            printLine(STR_ERR_NOTTZX, 1);
            TZXStop();
        }
    }
    else
    {
        printLine(STR_ERR_READING, 0);
    }
    bytesRead = 10;
}

void ReadAYHeader()
{
    //Read and check first 8 bytes for a TZX header
    char ayHeader[9];
    int i = 0;

    if (entry.seekSet(0))
    {
        i = entry.read(ayHeader, 8);
        if (memcmp_P(ayHeader, AYFile, 8) != 0)
        {
            printLine(STR_ERR_NOTAY, 1);
            TZXStop();
        }
    }
    else
    {
        printLine(STR_ERR_READING, 0);
    }
    bytesRead = 0;
}