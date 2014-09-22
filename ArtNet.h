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

#ifndef ARTNET_H
#define ARTNET_H

#include <Arduino.h>

// OEM_HI code taken from nomis52 ArtNet node
#define OEM_HI 0x04
// OEM_LO code is nomis52 ArtNet node + 1
#define OEM_LO 0x31

// Port to listen on
#define UDP_PORT_ARTNET       6454  /* (0x1936) */

typedef enum ArtNetPortTypeTag {
	ARTNET_IN,
	ARTNET_OUT
}
ArtNetPortType;

typedef enum { PRIMARY = 0, SECONDARY, DHCP, CUSTOM } IPConfiguration;

typedef enum ArtNetStatusTag
{
	ARTNET_STATUS_DEBUG = 0x0000,
	ARTNET_STATUS_POWER_OK = 0x0001,
	ARTNET_STATUS_POWER_FAIL = 0x0002,
	ARTNET_STATUS_READ_FAIL = 0x0003,
	ARTNET_STATUS_PARSE_FAIL = 0x0004,
	ARTNET_STATUS_WRITE_FAIL = 0x0005,
	ARTNET_STATUS_SHORT_NAME_SUCCESS = 0x0006,
	ARTNET_STATUS_LONG_NAME_SUCCESS = 0x0007,
	ARTNET_STATUS_DMX_ERRORS = 0x0008,
	ARTNET_STATUS_WRITE_BUFFER_FULL = 0x0009,
	ARTNET_STATUS_READ_BUFFER_FULL = 0x000a,
	ARTNET_STATUS_UNIVERSE_CONFLICT = 0x000b,
	ARTNET_STATUS_CONFIGURATION_FAIL = 0x000c,
	ARTNET_STATUS_DMX_OUTPUT_SHORT = 0x000d,
	ARTNET_STATUS_FIRMWARE_FAIL = 0x000e,
	ARTNET_STATIS_USER_FAIL = 0x000f
}
ArtNetStatus_t;

class ArtNet
{
  private:
    byte *ip;
    byte *mac;
    byte dhcp;
    byte eepromaddress;
    byte broadcastIP[4];
    byte serverIP[4];
    byte *buffer;
    word buflen;
    void (*sendFunc)(byte length, word sport, byte *dip, word dport);
    void (*callback)(unsigned short, const char *, unsigned short);
    void (*setIP)(IPConfiguration, const char*, const char*);
    unsigned char ArtNetDiagnosticPriority;
    unsigned char ArtNetDiagnosticStatus;
    unsigned int ArtNetCounter;
    unsigned int ArtNetInCounter;
    unsigned int ArtNetFailCounter;
    ArtNetStatus_t ArtNetStatus;
    char *ArtNetStatusString;
    unsigned char Universes;
    unsigned char *ArtNetInputPortStatus;
    unsigned char *ArtNetOutputPortStatus;
    unsigned char *ArtNetInputUniverse;
    unsigned char *ArtNetOutputUniverse;
    ArtNetPortType *ArtNetInputEnable;
    unsigned char ArtNetSubnet;

  public:
    ArtNet(byte *mac, byte eepromaddress, byte *buffer, word buflen, void (*setIP)(IPConfiguration, const char*, const char*), void (*sendFunc)(byte, word, byte*, word), void (*callback)(unsigned short, const char *, unsigned short), unsigned char universes);
    void Configure(byte dhcp, byte *ip);
    void ProcessPacket(byte ip[4], word port, const char *data, word len);
    void SendPoll(unsigned char force);
    void GetLongName(char *longName);
    void SetLongName(char *longName);
    void GetShortName(char *shortName);
    void SetShortName(char *shortName);
    unsigned char GetInputUniverse(unsigned char port);
    void SetInputUniverse(unsigned char port, unsigned char universe);
    unsigned char GetSubnet();
    void SetSubnet(unsigned char subnet);
    unsigned int GetPacketCount();
    unsigned int GetFailCount();
  private:
    void processPoll(byte ip[4], word port, const char *data, word len);
    void processAddress(byte ip[4], word port, const char *data, word len);
    void processInput(byte ip[4], word port, const char *data, word len);
    void sendIPProgReply(byte ip[4], word port);
    void processIPProg(byte ip[4], word port, const char *data, word len);
};

#endif
