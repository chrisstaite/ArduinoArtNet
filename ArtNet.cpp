/*
    ArtNet Library written for Arduino
    by Chris Staite, yourDream
    Copyright 2013

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ArtNet.h"
#include <EEPROM.h>

#define htons(x) ( ((x)<<8) | (((x)>>8)&0xFF) )

/**************************************************************************
 * Types
 **************************************************************************/

typedef enum ArtNetOpCodeTag
{
	ARTNET_OP_POLL = 0x2000,
	ARTNET_OP_POLL_REPLY = 0x2100,
	ARTNET_OP_DIAG_DATA = 0x2300,
	ARTNET_OP_OUTPUT = 0x5000,
	ARTNET_OP_ADDRESS = 0x6000,
	ARTNET_OP_INPUT = 0x7000,
	ARTNET_OP_TOD_REQUEST = 0x8000,
	ARTNET_OP_TOD_DATA = 0x8100,
	ARTNET_OP_TOD_CONTROL = 0x8200,
	ARTNET_OP_RDM = 0x8300,
	ARTNET_OP_RDM_SUB = 0x8400,
	ARTNET_OP_VIDEO_SETUP = 0xa010,
	ARTNET_OP_VIDEO_PALETTE = 0xa020,
	ARTNET_OP_VIDEO_DATA = 0xa040,
	ARTNET_OP_MAC_MASTER = 0xf000,
	ARTNET_OP_MAC_SLAVE = 0xf100,
	ARTNET_OP_FIRMWARE_MASTER = 0xf200,
	ARTNET_OP_FIRMWARE_REPLY = 0xf300,
	ARTNET_OP_IP_PROG = 0xf800,
	ARTNET_OP_IP_PROG_REPLY = 0xf900,
	ARTNET_OP_MEDIA = 0x9000,
	ARTNET_OP_MEDIA_PATCH = 0x9100,
	ARTNET_OP_MEDIA_CONTROL = 0x9200,
	ARTNET_OP_MEDIA_CONTROL_REPLY = 0x9300,
	ARTNET_OP_TIMECODE = 0x9700,
}
ArtNetOpCode;

typedef struct
{
	ArtNetOpCode opcode;
	
	uint8_t protocol_hi;
	uint8_t protocol_lo;
}
artnetheader_t;

typedef enum ArtNetPriorityTag
{
	ARTNET_DIAGNOSTIC_LOW = 0x10,
	ARTNET_DIAGNOSTIC_MED = 0x40,
	ARTNET_DIAGNOSTIC_HIGH = 0x80,
	ARTNET_DIAGNOSTIC_CRITICAL = 0xe0,
	ARTNET_DIAGNOSTIC_VOLATILE = 0xff
}
ArtNetPriority;

typedef enum ArtNetTalkToMeTag
{
	ARTNET_DIAGNOSTIC_BROADCAST = (1 << 3),  // Set if ArtNetPollReply should broadcast
	ARTNET_DIAGNOSTIC_SEND = (1 << 2),       // Set if diagnostic messages should be sent
	ARTNET_DIAGNOSTIC_ALWAYS = (1 << 1)      // Set if ArtNetPollReply should be sent whenever changes occur
}
ArtNetTalkToMe;

static char ARTNET_STATUS_STRING_OK[] = "Node Ok";
static char ArtNetMagic[] = "Art-Net";

/* Implementation */

ArtNet::ArtNet(byte *mac, byte eepromaddress, byte *buffer, word buflen, void (*setIP)(IPConfiguration, const char*, const char*), void (*sendFunc)(size_t, word, byte*, word), void (*callback)(unsigned short, const char *, unsigned short), unsigned char universes)
{
    unsigned char i;
    unsigned char v;
    
    this->broadcastIP[0] = 255;
    this->broadcastIP[1] = 255;
    this->broadcastIP[2] = 255;
    this->broadcastIP[3] = 255;
    this->serverIP[0] = 255;
    this->serverIP[1] = 255;
    this->serverIP[2] = 255;
    this->serverIP[3] = 255;
    this->mac = mac;
    this->eepromaddress = eepromaddress;
    
    this->buffer = buffer;
    this->buflen = buflen;
    this->sendFunc = sendFunc;
    this->callback = callback;
    this->setIP = setIP;
    this->Universes = universes;
    
    this->ArtNetDiagnosticPriority = ARTNET_DIAGNOSTIC_CRITICAL;
    this->ArtNetDiagnosticStatus = ARTNET_DIAGNOSTIC_BROADCAST | ARTNET_DIAGNOSTIC_SEND | ARTNET_DIAGNOSTIC_ALWAYS;
    this->ArtNetCounter = 0;
    this->ArtNetStatus = ARTNET_STATUS_POWER_OK;
    this->ArtNetStatusString = ARTNET_STATUS_STRING_OK;
    v = EEPROM.read(eepromaddress);
    if (v != 253) EEPROM.write(eepromaddress, 253);
    if (v != 253) EEPROM.write(eepromaddress + 1 + 18 + 64, 0);
    this->ArtNetSubnet = EEPROM.read(eepromaddress + 1 + 18 + 64);
    if (this->ArtNetSubnet == 0xff) this->ArtNetSubnet = 0;
    this->ArtNetInCounter = 0;
    this->ArtNetFailCounter = 0;
    
    this->ArtNetInputPortStatus = (unsigned char*)calloc(1, universes);
    this->ArtNetOutputPortStatus = (unsigned char*)calloc(1, universes);
    this->ArtNetInputUniverse = (unsigned char*)calloc(1, universes);
    this->ArtNetOutputUniverse = (unsigned char*)calloc(1, universes);
    this->ArtNetInputEnable = (ArtNetPortType*)calloc(sizeof(ArtNetPortType), universes);
    for (i = 0; i < universes; ++i) {
        if (v != 253) EEPROM.write(eepromaddress + 1 + 18 + 64 + 2 + i, 0);
        this->ArtNetInputUniverse[i] = EEPROM.read(eepromaddress + 1 + 18 + 64 + 2 + i);
        if (v != 253) EEPROM.write(eepromaddress + 1 + 18 + 64 + 2 + universes + i, 0);
        this->ArtNetOutputUniverse[i] = EEPROM.read(eepromaddress + 1 + 18 + 64 + 2 + universes + i);
        if (v != 253) EEPROM.write(eepromaddress + 1 + 18 + 64 + 2 + universes + universes + i, ARTNET_IN);
        this->ArtNetInputEnable[i] = (ArtNetPortType)EEPROM.read(eepromaddress + 1 + 18 + 64 + 2 + universes + universes + i);
    }
    
    /* Clear names if uninitialised */
    if (v != 253) {
        for (i = 0; i < 18; ++i) {
            EEPROM.write(eepromaddress + 1 + i, 0);
        }
        for (i = 0; i < 64; ++i) {
            EEPROM.write(eepromaddress + 1 + 18 + i, 0);
        }
    }
}

void ArtNet::Configure(byte dhcp, byte* ip)
{
    unsigned char i;
    
    this->ip = ip;
    this->dhcp = dhcp;
    
    if (EEPROM.read(eepromaddress + 1 + 18 + 64 + 1) == 1) {
        // Reboot due to IP change
        EEPROM.write(eepromaddress + 1 + 18 + 64 + 1, 0);
        byte sendIp[4];
        word sendPort;
        for (i = 0; i < 4; ++i) {
            sendIp[i] = EEPROM.read(eepromaddress + 1 + 18 + 64 + 2 + this->Universes * 3 + i);
        }
        for (i = 0; i < 2; ++i) {
            ((byte*)&sendPort)[i] = EEPROM.read(eepromaddress + 1 + 18 + 64 + 2 + this->Universes * 3 + 4 + i);
        }
    	this->sendIPProgReply(sendIp, sendPort);
    } else {
        // Standard boot
        this->SendPoll(1);
    }
}

void ArtNet::GetShortName(char *shortName)
{
    unsigned char i;
    for (i = 0; i < 18; ++i) {
        shortName[i] = EEPROM.read(this->eepromaddress + 1 + i);
    }
}

void ArtNet::SetShortName(char *shortName)
{
    unsigned char i;
    for (i = 0; i < 18; ++i) {
        EEPROM.write(this->eepromaddress + 1 + i, shortName[i]);
        if (!shortName[i]) break;
    }
    for (; i < 18; ++i) {
        EEPROM.write(this->eepromaddress + 1 + i, 0);
    }
}

void ArtNet::GetLongName(char *longName)
{
    unsigned char i;
    for (i = 0; i < 64; ++i) {
        longName[i] = EEPROM.read(this->eepromaddress + 1 + 18 + i);
    }
}

void ArtNet::SetLongName(char *longName)
{
    unsigned char i;
    for (i = 0; i < 64; ++i) {
        EEPROM.write(this->eepromaddress + 1 + 18 + i, longName[i]);
        if (!longName[i]) break;
    }
    for (; i < 64; ++i) {
        EEPROM.write(this->eepromaddress + 1 + 18 + i, 0);
    }
}

unsigned char ArtNet::GetInputUniverse(unsigned char port)
{
    if (port >= this->Universes) return 0;
    return this->ArtNetInputUniverse[port];
}

void ArtNet::SetInputUniverse(unsigned char port, unsigned char universe)
{
    if (port >= this->Universes) return;
    this->ArtNetInputUniverse[port] = universe;
    EEPROM.write(this->eepromaddress + 1 + 18 + 64 + 2 + port, universe);
}

unsigned char ArtNet::GetSubnet()
{
    return this->ArtNetSubnet;
}

void ArtNet::SetSubnet(unsigned char subnet)
{
    this->ArtNetSubnet = subnet;
    EEPROM.write(this->eepromaddress + 1 + 18 + 64, subnet);
}

unsigned int ArtNet::GetPacketCount()
{
    return this->ArtNetInCounter;
}

unsigned int ArtNet::GetFailCount()
{
    return this->ArtNetFailCounter;
}

void ArtNet::processPoll(byte ip[4], word port, const char *data, word len)
{
	// Read data
	data += sizeof(artnetheader_t) + sizeof(ArtNetMagic);
	
	memcpy(&this->serverIP, ip, 4);

	this->ArtNetDiagnosticStatus = data[0];
	this->ArtNetDiagnosticPriority = data[1];

    this->SendPoll(1);
}

void ArtNet::processAddress(byte ip[4], word port, const char *data, word len)
{
	unsigned char i;
	
	// Read data
	data += sizeof(artnetheader_t) + sizeof(ArtNetMagic);

	// Set the short name
	// Check if the name is null
    if (data[2]) {
    	// Let's set the short name
	    for (i = 0; i < 18; ++i)
	        EEPROM.write(this->eepromaddress + 1 + i, data[2 + i]);
    }
	
	// Set the long name
	// Check if the name is null
    if (data[19]) {
    	// Let's set the short name
	    for (i = 0; i < 18; ++i)
	        EEPROM.write(this->eepromaddress + 1 + 18 + i, data[2 + 18 + i]);
    }
    
    // Set input universes
    for (i = 0; i < this->Universes; i++) {
		if (data[64 + 19 + 1 + i] != 0x7f && (data[64 + 19 + 1 + i] & (1 << 7))) {
			unsigned char t;
			// Only set if bit 7 is high
			t = data[64 + 19 + 1 + i] & ~(1 << 7);
			if (this->ArtNetInputUniverse[i] != t) {
				this->ArtNetInputUniverse[i] = t;
				EEPROM.write(this->eepromaddress + 1 + 18 + 64 + 2 + i, t);
			}
		}
    }
    
    // Set output universes
    for (i = 0; i < this->Universes; i++) {
		if (data[64 + 19 + 1 + this->Universes + i] != 0x7f && (data[64 + 19 + 1 + this->Universes + i] & (1 << 7))) {
			unsigned char t;
			// Only set if bit 7 is high
			t = data[64 + 19 + 1 + this->Universes + i] & ~(1 << 7);
			if (this->ArtNetOutputUniverse[i] != t) {
				this->ArtNetOutputUniverse[i] = t;
				EEPROM.write(this->eepromaddress + 1 + 18 + 64 + 2 + this->Universes + i, t);
			}
		}
    }
    
    // Set subnet
	if (data[64 + 19 + 1 + this->Universes + this->Universes] != 0x7f && data[64 + 19 + 1 + this->Universes + this->Universes] & (1 << 7)) {
		unsigned char t;
		t = data[64 + 19 + 1 + this->Universes + this->Universes] & ~(1 << 7);
		if (this->ArtNetSubnet != t) {
			this->ArtNetSubnet = t;
			EEPROM.write(this->eepromaddress + 1 + 18 + 64, t);
		}
	}
	
    // Command - mostly ignored because we don't support any merging
	switch (data[64 + 19 + 1 + this->Universes + this->Universes + 2]) {
		case 0x90:
		case 0x91:
		case 0x92: {
			unsigned char t;
			t = data[64 + 19 + 1 + this->Universes + this->Universes + 2] & 0x3;
			// Reset data on port t
			break;
		}
	}
	
	this->SendPoll(1);
}

void ArtNet::processInput(byte ip[4], word port, const char *data, word len)
{
	// Read data
	data += sizeof(artnetheader_t) + sizeof(ArtNetMagic);
	unsigned char i;
	
	for (i = 0; i < this->Universes; i++) {
		if (this->ArtNetInputEnable[i] != (data[4 + i] & 1)) {
			// Configure as input
			if (this->ArtNetInputEnable[i] != (data[4 + i] & 1)) {
				this->ArtNetInputEnable[i] = (data[4 + i] & 1) ? ARTNET_OUT : ARTNET_IN;
				EEPROM.write(this->eepromaddress + 1 + 18 + 64 + this->Universes + this->Universes + i, this->ArtNetInputEnable[i]);
			}
			// Reconfigure port
			if (data[4 + i] & 1) {
			    // Port i - Input
			} else {
			    // Port i - Output
			}
		}
	}
}

void ArtNet::sendIPProgReply(byte ip[4], word port)
{
    size_t length = 0;
    unsigned short t16;

    // Magic
    memcpy(this->buffer, ArtNetMagic, sizeof(ArtNetMagic));
    length += sizeof(ArtNetMagic);

    // Op code
    t16 = htons(ARTNET_OP_IP_PROG_REPLY);
    memcpy(&this->buffer[length], &t16, 2);
    length += 2;

    // Version
    t16 = htons(14);
    memcpy(&this->buffer[length], &t16, 2);
    length += 2;

    // Padding
    memset(&this->buffer[length], 0, 4);
    length += 4;

    // Node IP
    memcpy(&this->buffer[length], this->ip, 4);
    length += 4;
    
    // Node subnet
    memset(&this->buffer[length], 0, 4);
    length += 4;

    // Port
    t16 = htons(UDP_PORT_ARTNET);
    memcpy(&this->buffer[length], &t16, 2);
    length += 2;
    
    // Status (DHCP enabled?)
    this->buffer[length++] = this->dhcp;
    
    // Spare/Filler
    memset(&this->buffer[length], 0, 7);
    length += 7;

    // Transmit ArtNetIpProgReply
    this->sendFunc(length, UDP_PORT_ARTNET_REPLY, ip, port);
}

void ArtNet::processIPProg(byte ip[4], word port, const char *data, word len)
{
    unsigned char i;
    IPConfiguration type;
    const char *newip = 0;
    const char *subnet = 0;
    
	// Read data
	data += sizeof(artnetheader_t) + sizeof(ArtNetMagic);
	
	// Process command
	if (!(data[2] & (1 << 7))) {
		// No programming enabled
		this->sendIPProgReply(ip, port);
		return;
	}
	
	if (data[2] & (1 << 6)) {
		// Enable DHCP
		if (this->dhcp != 1)
			type = DHCP;
	}
	
	if (data[2] & (1 << 3)) {
		// Set to default
		type = PRIMARY;
	}
	
	// Read four bytes
	if (data[2] & (1 << 2)) {
		newip = data + 4;
	}

	if (data[2] & (1 << 1)) {
		subnet = data + 8;
	}
	
	if (data[2] & 1) {
		// Program port - ignore this for now
	}
	
	// Set eeprom bit
	EEPROM.write(this->eepromaddress + 1 + 18 + 64 + 1, 1);
	for (i = 0; i < 4; ++i) {
	    EEPROM.write(this->eepromaddress + 1 + 18 + 64 + 2 + this->Universes + this->Universes + this->Universes + i, ip[i]);
	}
	for (i = 0; i < 2; ++i) {
	    EEPROM.write(this->eepromaddress + 1 + 18 + 64 + 2 + this->Universes + this->Universes + this->Universes + 4 + i, ((byte*)&port)[i]);
	}
	// Save (and reboot)
	this->setIP(type, newip, subnet);
}

void ArtNet::ProcessPacket(byte ip[4], word port, const char *data, word len)
{
	artnetheader_t *header;
	
	if (strncmp(data, ArtNetMagic, 7) != 0) {
		this->ArtNetFailCounter++;
		return;
	}
	
	this->ArtNetInCounter++;

	header = (artnetheader_t*)(data + sizeof(ArtNetMagic));
    
    if (header->protocol_lo < 14) {
    	return;
    }

    switch (header->opcode) {

    	/* Input and Configuration */
    	
    	case ARTNET_OP_POLL:
    		this->processPoll(ip, port, data, len);
    		break;
		case ARTNET_OP_OUTPUT:
			{
				unsigned short universe, length;
				unsigned char i;

				// Read data
				data += sizeof(artnetheader_t) + sizeof(ArtNetMagic);
	
			    universe = data[2] | (data[3] << 8);
    
			    // Length
			    length = htons(data[4]);
    
			    for (i = 0; i < this->Universes; i++) {
			    	if (this->ArtNetInputUniverse[i] == universe && this->ArtNetInputEnable[i] == ARTNET_IN) {
			    		// Set Data for this output
			    		// Port i - d[6 + j] (j = 0 to length)
			    		this->callback(i, &data[6], length);
			    	}
			    }
			}
			break;
		case ARTNET_OP_ADDRESS:
			this->processAddress(ip, port, data, len);
			break;
		case ARTNET_OP_INPUT:
			this->processInput(ip, port, data, len);
			break;
		case ARTNET_OP_IP_PROG:
			this->processIPProg(ip, port, data, len);
			break;

		/* Undocumented - for manufacturer use (that's me!) */

		case ARTNET_OP_MAC_MASTER:
			break;
		case ARTNET_OP_MAC_SLAVE:
			break;

		/* RDM - Currently unsupported */

		case ARTNET_OP_RDM:
			break;
		case ARTNET_OP_RDM_SUB:
			break;
		case ARTNET_OP_TOD_REQUEST:
			break;
		case ARTNET_OP_TOD_DATA:
			break;
		case ARTNET_OP_TOD_CONTROL:
			break;
			
		/* Ignored broadcasted reply op codes */
		
		case ARTNET_OP_POLL_REPLY:
			// Ignore replies
		case ARTNET_OP_IP_PROG_REPLY:
			// Ignore IP Programming replies
		case ARTNET_OP_DIAG_DATA:
			// Ignore diagnostics
			break;
			
		/* Unsupported feature op codes */
		
		case ARTNET_OP_TIMECODE:
			break;

		case ARTNET_OP_FIRMWARE_MASTER:
		case ARTNET_OP_FIRMWARE_REPLY:
			// Don't have the capability to do OTW firmware update
			break;
			
		case ARTNET_OP_VIDEO_SETUP:
		case ARTNET_OP_VIDEO_PALETTE:
		case ARTNET_OP_VIDEO_DATA:
			// Ignore video data
			break;
			
		case ARTNET_OP_MEDIA:
		case ARTNET_OP_MEDIA_PATCH:
		case ARTNET_OP_MEDIA_CONTROL:
		case ARTNET_OP_MEDIA_CONTROL_REPLY:
			// Ignore media data
			break;
		
		/* Unknown op code */
		
		default:
			ArtNetStatus = ARTNET_STATUS_PARSE_FAIL;
			this->SendPoll(0);
			return;
    }
}

void ArtNet::SendPoll(unsigned char force)
{
    byte *destIp;
    size_t length = 0;
    unsigned int t16;

	if (!force && !(this->ArtNetDiagnosticStatus & ARTNET_DIAGNOSTIC_ALWAYS)) {
		// We are not forcing (i.e. not replying to ArtPoll) and not always sending updates
		return;
	}
	
	if (!force) {
		// Increment the non-requested poll counter
		this->ArtNetCounter++;
	}

	if (this->ArtNetDiagnosticStatus & ARTNET_DIAGNOSTIC_BROADCAST) {
		destIp = this->broadcastIP;
	} else {
		destIp = this->serverIP;
	}

    // Magic
    memcpy(&this->buffer[length], ArtNetMagic, sizeof(ArtNetMagic));
    length += sizeof(ArtNetMagic);
    
    // Op code
    t16 = ARTNET_OP_POLL_REPLY;
    memcpy(&this->buffer[length], &t16, 2);
    length += 2;
    
    // Transmit IP
    memcpy(&this->buffer[length], this->ip, 4);
    length += 4;
    
    // Port
    t16 = UDP_PORT_ARTNET;
    memcpy(&this->buffer[length], &t16, 2);
    length += 2;
    
    // Version
    t16 = htons(14);
    memcpy(&this->buffer[length], &t16, 2);
    length += 2;

    // Subnet
    t16 = htons(this->ArtNetSubnet);
    memcpy(&this->buffer[length], &t16, 2);
    length += 2;
    
    // OEM
    this->buffer[length++] = OEM_HI;
    this->buffer[length++] = OEM_LO;
    
    // UBEA
    this->buffer[length++] = 0;
    
    // Status 1
    this->buffer[length] = 0x3 << 6; // Indicators in Normal mode
    this->buffer[length] = this->buffer[length] | (0x2 << 4); // Universe programmed by network
    // t8 = t8 | (0x1 << 1); // RDM capable
    ++length;
    
    // ESTA (YD)
    t16 = 0x5944;
    memcpy(&this->buffer[length], &t16, 2);
    length += 2;
    
    // Names
    {
        // Short name (18 bytes)
        for (t16 = 0; t16 < 18; ++t16)
            this->buffer[length++] = EEPROM.read(this->eepromaddress + 1 + t16);
        // Long name (64 bytes)
        for (t16 = 0; t16 < 64; ++t16)
            this->buffer[length++] = EEPROM.read(this->eepromaddress + 1 + 18 + t16);
    }
    
    // Report
    {
        snprintf((char*)&this->buffer[length], 64, "#%x %d %s", this->ArtNetStatus, this->ArtNetCounter, this->ArtNetStatusString);
        length += 64;
    }
    
    // Number of DMX ports
    t16 = htons(this->Universes);
    memcpy(&this->buffer[length], &t16, 2);
    length += 2;
    
    // Port Configuration
    // Port 1-4
    for (t16 = 0; t16 < this->Universes; ++t16) {
        this->buffer[length++] = 0xc0; // Input and output port over DMX512
    }
    
    // Port input status
    for (t16 = 0; t16 < this->Universes; ++t16) {
        this->buffer[length++] = ArtNetInputPortStatus[t16];
    }
    
    // Port output status
    for (t16 = 0; t16 < this->Universes; ++t16) {
        this->buffer[length++] = ArtNetOutputPortStatus[t16];
    }
    
    // Port input universe
    for (t16 = 0; t16 < this->Universes; ++t16) {
        this->buffer[length++] = ArtNetInputUniverse[t16];
    }
    
    // Port output universe
    for (t16 = 0; t16 < this->Universes; ++t16) {
        this->buffer[length++] = ArtNetOutputUniverse[t16];
    }
    
    // Video, Macro and Remote followed by three spare and Style (StNode = 0)
    memset(&this->buffer[length], 0, 1 + 1 + 1 + 3 + 1);
    length += 1 + 1 + 1 + 3 + 1;
    
    {
        // MAC Address
        memcpy(&this->buffer[length], this->mac, 6);
        length += 6;
    }
    
    // Bind IP, set to the same as self IP
    memcpy(&this->buffer[length], this->ip, 4);
    length += 4;
    
    // Bind Index - Root node, so 0
    this->buffer[length++] = 0;
    
    // Status 2
    this->buffer[length] = 1; // Web browser configuration supported
    this->buffer[length] = this->buffer[length] | (this->dhcp << 1); // DHCP is enabled
    this->buffer[length] = this->buffer[length] | (1 << 2); // DHCP supported
    ++length;
    
    // Filler
    memset(&this->buffer[length], 0, 26);
    length += 26;

    // Transmit ArtNetPollReply
    this->sendFunc(length, UDP_PORT_ARTNET, destIp, UDP_PORT_ARTNET_REPLY);

    // Reset status
    this->ArtNetStatus = ARTNET_STATUS_POWER_OK;
    this->ArtNetStatusString = ARTNET_STATUS_STRING_OK;
}
