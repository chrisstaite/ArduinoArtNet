
#include <Serial.h>
#include <EtherCard.h>
#include <EEPROM.h>
#include <ArtNet.h>
#include <FastLED.h>

#define DEFAULT_NUM_LEDS 128
#define DEFAULT_START_ADDRESS 0
#define PORTS 1 // Number of ports to use
#define CHIPSET TM1809
#define COLOUR_ORDER RGB

#define DATA_PIN A5 

CRGB *leds;

// Set a different MAC address for each...
static byte mymac[] = { 0x74, 0x69, 0x69, 0x2D, 0x30, 0x32 };
byte Ethernet::buffer[600]; // tcp/ip send and receive buffer

struct Config {
    IPConfiguration iptype;
    byte ip[4];
    byte gateway[4];
    unsigned short connectedLEDs;
    unsigned short startAddress;
    byte valid;
} config;

ArtNet artnet(mymac, sizeof(config) + 1, Ethernet::buffer + UDP_DATA_P, sizeof(Ethernet::buffer) - UDP_DATA_P, setIP, artSend, callback, PORTS);

// Calling 0 breaks the processor causing it to soft reset
void(* resetFunc) (void) = 0; 

static void saveConfig() {
  for (unsigned short i = 0; i < sizeof(config); ++i) {
    EEPROM.write(i, ((byte*)&config)[i]);
  }
}

static void loadConfig() {
  for (unsigned short i = 0; i < sizeof(config); ++i) {
    ((byte*)&config)[i] = EEPROM.read(i);
  }
  if (config.valid != 252) {
    // Load defaults
    config.iptype = DHCP;
    config.connectedLEDs = DEFAULT_NUM_LEDS;
    config.startAddress = DEFAULT_START_ADDRESS;
    config.valid = 252;
    saveConfig();
  }
}

static void setIP(IPConfiguration iptype, const char *ip, const char *subnet)
{
  config.iptype = iptype;
  memcpy(config.ip, ip, 4);
  // Subnet too??
  // What about the gateway - not really important for ArtNet!
  saveConfig();
  // Restart the chip to load the new configuration
  resetFunc();
}

static void artSend(size_t length, word sport, byte *dip, word dport)
{
  ether.sendUdp((char*)Ethernet::buffer + UDP_DATA_P, length, sport, dip, dport);
}

static void callback(unsigned short port, const char *buffer, unsigned short length)
{
  if (length < config.startAddress) return;
  if (port != 0) return;
  length -= config.startAddress;
  buffer = buffer + config.startAddress;
  length = length / 3;
  if (length > config.connectedLEDs) length = config.connectedLEDs;
  for (int i = 0; i < length; ++i)
  {
    leds[i].r = buffer[i * 3];
    leds[i].g = buffer[i * 3 + 1];
    leds[i].b = buffer[i * 3 + 2];
  }
  FastSPI_LED.show();
}

static void artnetPacket(word port, byte ip[4], const char *data, word len) {
  artnet.ProcessPacket(ip, port, data, len);
}

void setup() {
  Serial.begin(57600);
  Serial.println(F("\nBooting"));
  
  // Load configuration
  Serial.println(F("Loading configuration"));
  loadConfig();
  
  // Setup LEDS
  Serial.println(F("Configuring LEDs"));
  leds = new CRGB[config.connectedLEDs];
  FastLED.addLeds<CHIPSET, DATA_PIN, COLOUR_ORDER>(leds, config.connectedLEDs);
  
  Serial.println(F("Initialising LEDs"));
  
  Serial.println(F("Clearing LEDs"));
  memset(leds, 0, sizeof(CRGB) * config.connectedLEDs);
  FastLED.show();
  
  // Startup ethernet
  Serial.println(F("Initialising ENC28J60"));
  if (ether.begin(sizeof(Ethernet::buffer), mymac) == 0) 
    Serial.println(F("Failed to access Ethernet controller"));
    
  // Configure IP address
  if (config.iptype == DHCP) {
    Serial.println(F("Configuring node as DHCP"));
    if (!ether.dhcpSetup())
      Serial.println(F("DHCP Failed..."));
    ether.printIp("IP: ", ether.myip);
    ether.printIp("GW: ", ether.gwip);
  } else if (config.iptype == CUSTOM) {
    Serial.println(F("Configuring node with custom IP"));
    ether.staticSetup(config.ip, config.gateway);
    ether.printIp("IP: ", ether.myip);
    ether.printIp("GW: ", ether.gwip);
  } else if (config.iptype == PRIMARY) {
    byte ip[] = {2, mymac[3]+OEM_HI+OEM_LO, mymac[4], mymac[5]};
    byte gwy[] = {2, 0, 0, 1};
    Serial.println(F("Configuring node with primary IP"));
    ether.staticSetup(ip, gwy);
    ether.printIp("IP: ", ether.myip);
    ether.printIp("GW: ", ether.gwip);
  } else if (config.iptype == SECONDARY) {
    byte ip[] = {10, mymac[3]+OEM_HI+OEM_LO, mymac[4], mymac[5]};
    byte gwy[] = {10, 0, 0, 1};
    Serial.println(F("Configuring node with secondary IP"));
    ether.staticSetup(ip, gwy);
    ether.printIp("IP: ", ether.myip);
    ether.printIp("GW: ", ether.gwip);
  } else {
    Serial.println(F("loadConfig() is doing its job wrong"));
  }
  
  Serial.println(F("Configuring ArtNet"));
  artnet.Configure(config.iptype == DHCP, ether.myip);
  
  // Register listener
  Serial.println(F("Listening on ArtNet"));
  /* Also need to change line 110 of tcpip.cpp to:
                       (memcmp(gPB + IP_DST_P, EtherCard::myip, 4) == 0 
                          || gPB[IP_DST_P + 3] == 0xff); */
  ether.enableBroadcast();
  ether.udpServerListenOnPort(&artnetPacket, UDP_PORT_ARTNET);
}

char statusPage[] PROGMEM =
"HTTP/1.0 200 Ok\r\n"
"Content-Type: text/html\r\n"
"Pragma: no-cache\r\n"
"\r\n"
"<html>"
  "<head><title>"
    "ArtNet Node Configuration"
  "</title></head>"
  "<body>"
    "<h1>ArtNet Node Configuration</h1>"
    "<h2>Packets: %d (%d failed)</h2>"
    "<p><a href='/ip'>IP Configuration</a></p>"
    "<p><a href='/artnet'>ArtNet Configuration</a></p>"
    "<p><a href='/led'>LED Configuration</a></p>"
  "</body>"
"</html>";

char ipPage[] PROGMEM =
"HTTP/1.0 200 Ok\r\n"
"Content-Type: text/html\r\n"
"Pragma: no-cache\r\n"
"\r\n"
"<html>"
  "<head><title>"
    "ArtNet Node"
  "</title></head>"
  "<body>"
    "<h1>IP Configuration</h1>"
    "<a href='/'>Back</a><p/>"
    "<form action='/ip'>"
      "IP Type:<select name='iptype'>"
        "<option value='2'%s>DHCP</option>"
        "<option value='0'%s>Pri</option>"
        "<option value='1'%s>Sec</option>"
        "<option value='3'%s>Custom</option>"
      "</select><p/>"
      "IP:<input type='text' name='ip' value='%d.%d.%d.%d'><p/>"
      "<input type='submit'>"
    "</form>"
  "</body>"
"</html>";

char artnetPage[] PROGMEM =
"HTTP/1.0 200 Ok\r\n"
"Content-Type: text/html\r\n"
"Pragma: no-cache\r\n"
"\r\n"
"<html>"
  "<head><title>"
    "ArtNet Node"
  "</title></head>"
  "<body>"
    "<h1>ArtNet Configuration</h1>"
    "<a href='/'>Back</a><p/>"
    "<form action='/artnet'>"
      "Short Name:<input type='text' name='shortname' value='%s'><p/>"
      "Long Name:<input type='text' name='longname' value='%s'><p/>"
      "ArtNet Subnet:<input type='text' name='subnet' value='%d'><p/>"
      "Universe:<input type='text' name='universe' value='%d'><p/>"
      "<input type='submit'>"
    "</form>"
  "</body>"
"</html>";

char ledPage[] PROGMEM =
"HTTP/1.0 200 Ok\r\n"
"Content-Type: text/html\r\n"
"Pragma: no-cache\r\n"
"\r\n"
"<html>"
  "<head><title>"
    "ArtNet Node"
  "</title></head>"
  "<body>"
    "<h1>LED Configuration</h1>"
    "<a href='/'>Back</a><p/>"
    "<form action='/led'>"
      "Connected LEDs:<input type='text' name='leds' value='%d'><p/>"
      "Start Address:<input type='text' name='address' value='%d'><p/>"
      "<input type='submit'>"
    "</form>"
  "</body>"
"</html>";

void sendHomePage() {
  unsigned short len = sprintf_P((char*)ether.tcpOffset(), statusPage,
      artnet.GetPacketCount(),
      artnet.GetFailCount());
  ether.httpServerReply(len);
}

void sendIPPage() {
  unsigned short len = sprintf_P((char*)ether.tcpOffset(), ipPage,
      config.iptype == DHCP ? " selected='selected'" : "",
      config.iptype == PRIMARY ? " selected='selected'" : "",
      config.iptype == SECONDARY ? " selected='selected'" : "",
      config.iptype == CUSTOM ? " selected='selected'" : "",
      ether.myip[0], ether.myip[1], ether.myip[2], ether.myip[3]);
  ether.httpServerReply(len);
}

void sendArtNetPage() {
  char shortName[19] = {0};
  char longName[65] = {0};
  artnet.GetShortName(shortName);
  artnet.GetLongName(longName);
  unsigned short len = sprintf_P((char*)ether.tcpOffset(), artnetPage,
      shortName,
      longName,
      artnet.GetSubnet(),
      artnet.GetInputUniverse(0));
  ether.httpServerReply(len);
}

void sendLEDPage() {
  unsigned short len = sprintf_P((char*)ether.tcpOffset(), ledPage,
      config.connectedLEDs,
      config.startAddress + 1);
  ether.httpServerReply(len);
}

static int getIntArg(const char* data, const char* key, int value =-1) {
    char temp[10];
    if (ether.findKeyVal(data, temp, sizeof temp, key) > 0)
        value = atoi(temp);
    return value;
}

static void setIpArg(const char *data, const char *key) {
  char ip[16];
  if (ether.findKeyVal(data, ip, sizeof(ip), key) > 0) {
    byte i;
    byte parsedIp[4];
    char *p = strtok(ip, ".");
    if (p == NULL) return;
    parsedIp[0] = atoi(p);
    for (i = 1; i < 4; ++i) {
      p = strtok(NULL, ".");
      if (p == NULL) return;
      parsedIp[i] = atoi(p);
    }
    memcpy(config.ip, parsedIp, 4);
  } 
}

static void setShortName(const char *data, const char *key) {
  char shortName[19];
  if (ether.findKeyVal(data, shortName, sizeof(shortName), key) > 0) {
    ether.urlDecode(shortName);
    artnet.SetShortName(shortName);
  }
}

static void setLongName(const char *data, const char *key) {
  char longName[65];
  if (ether.findKeyVal(data, longName, sizeof(longName), key) > 0) {
    ether.urlDecode(longName);
    artnet.SetLongName(longName);
  }
}

void loop() {
  word pos = 0;
  if ((pos = ether.packetLoop(ether.packetReceive()))) {
    if (strncmp("GET / ", (const char *)(Ethernet::buffer + pos), 6) == 0) {
      // Page emmited
      sendHomePage();
    } else if (strncmp("GET /ip ", (const char *)(Ethernet::buffer + pos), 8) == 0) {
      // Page emmited
      sendIPPage();
    } else if (strncmp("GET /artnet ", (const char *)(Ethernet::buffer + pos), 12) == 0) {
      // Page emmited
      sendArtNetPage();
    } else if (strncmp("GET /led ", (const char *)(Ethernet::buffer + pos), 9) == 0) {
      // Page emmited
      sendLEDPage();
    } else if (strncmp("GET /artnet?", (const char *)(Ethernet::buffer + pos), 12) == 0) {
      // Save settings
      artnet.SetInputUniverse(0, getIntArg((const char *)(Ethernet::buffer + pos + 11), "universe", artnet.GetInputUniverse(0)));
      artnet.SetSubnet(getIntArg((const char *)(Ethernet::buffer + pos + 11), "subnet", artnet.GetSubnet()));
      setShortName((const char *)(Ethernet::buffer + pos + 11), "shortname");
      setLongName((const char *)(Ethernet::buffer + pos + 11), "longname");
      
      // Send page with new settings
      sendArtNetPage();
    }  else if (strncmp("GET /led?", (const char *)(Ethernet::buffer + pos), 9) == 0) {
      // Save settings
      config.connectedLEDs = getIntArg((const char *)(Ethernet::buffer + pos + 11), "leds", config.connectedLEDs);
      config.startAddress = getIntArg((const char *)(Ethernet::buffer + pos + 11), "address", config.startAddress + 1) - 1;
      saveConfig();
      
      // Send page with new settings
      sendLEDPage();
    } else if (strncmp("GET /ip?", (const char *)(Ethernet::buffer + pos), 8) == 0) {
      // Save settings
      config.iptype = (IPConfiguration)getIntArg((const char *)(Ethernet::buffer + pos + 7), "iptype", config.iptype);
      setIpArg((const char *)(Ethernet::buffer + pos + 7), "ip");
      saveConfig();
      
      // Send page with new settings
      sendIPPage();
      // Restart the chip to load the new configuration
      resetFunc();
    }
  }
}

