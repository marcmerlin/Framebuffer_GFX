#ifndef WIFI_SERIAL_OTA_H
#define WIFI_SERIAL_OTA_H

// This is limited to 10 Wifi Clients by the arduino Wifi Core but defaults to 4
#define MAX_SLAVES 10

#define AP_MAX_CLIENTS 10

// May make things more stable but costs more power
// In my testing on a bit network, the slave can miss the master broadcasts
// while it's sleeping
#define WIFINOSLEEP

#define WAVESHARE_MASTER_PIN 7  // top left, client/slave mode after client to WIFI_SSID fails
#define WAVESHARE_FORCEAP_PIN 9 // below pin8, force creating AP even when WIFI_SSID is there

#if __has_include("wifi_secrets.h")
    #include "wifi_secrets.h"
#else
    #pragma message "wifi_secrets.h missing, you won't like the defaults :)"
#endif

#if !defined(WIFI_SSID)
    #error "please define WIFI vars by #define or in wifi_secrets.h"
#endif

// you define WIFI_HOSTNAME before doing #include "wifi_serial_ota.h"
// Also define WIFI_SSID and WIFI_PASSWORD before you include
// However, this is now only left in case you want to rip out EEPROM support
// as hostname is stored in EEPROM so it can be changed at runtime on each device
#ifndef WIFI_HOSTNAME
#define WIFI_HOSTNAME "CHANGEME WIFI_HOSTNAME"
#endif

#ifndef WIFI_OTA_PWD
#define WIFI_OTA_PWD "CHANGEME"
#endif

#ifndef WIFI_BEACON_ID
#define WIFI_BEACON_ID "BKCHANGEME" // Max 11 chars
#endif

#ifndef TELNET_PORT
#define TELNET_PORT 23
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "yourhomewifi"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "youwifipwd"
#endif

#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID "LED"
#endif

#ifndef WIFI_AP_PASSWORD
#define WIFI_AP_PASSWORD "Ledpassword"
#endif

#ifndef WIFI_AP_CHANNEL
#define WIFI_AP_CHANNEL 1
#endif

volatile bool wifi_network_stable = false;


#include <WiFi.h>
/// used for EVERY_N_SECONDS
#include "FastLED.h"
String get_hostname();

// ####################################################################
// Serial network splitter. Anything below here will get network serial
// ####################################################################

template <typename T>
class GenericSerialSplitter : public Stream {
private:
  T* hw;
  WiFiServer* server;
  WiFiClient client;
  bool serverStarted = false; 
  NetworkUDP udpServer;
  bool udpStarted = false;
  uint16_t udpPort;
  
  // Cooldown timer to prevent UDP polling from starving the network stack
  unsigned long lastUdpCheck = 0;
  const unsigned long UDP_COOLDOWN_MS = 100; // 10Hz limit

public:
  GenericSerialSplitter(T& hardwareSerial, uint16_t port = 23) {
    hw = &hardwareSerial;
    server = new WiFiServer(port);
    udpPort = port;
  }

  // Pass the initialization through to the underlying serial interface
  void begin(unsigned long baud) {
    #ifdef ESP32
      hw->setTxBufferSize(2048);
    #endif
    hw->begin(baud);
    // TinyUSB CDC: prevent blocking when host port is closed
    // Hardware USB Serial/JTAG (HWCDC): drop immediately if 2KB ring buffer is full and no host reads
    #if ARDUINO_USB_CDC_ON_BOOT
    hw->setTxTimeoutMs(0);
    #endif
  }

    // Boolean operator so "if (Serial)" and "while (!Serial)" evaluate cleanly
  operator bool() const {
    return hw ? (bool)(*hw) : false;
  }

  void restart() {
    Serial.println("Inside SerialSplitter.restart()");
    if (serverStarted) {
        server->end();
        serverStarted = false;
        Serial.println("SerialSplitter.restart(): end Splitter");
    }
    if (client) {
        client.stop();
        Serial.println("SerialSplitter.restart(): stop client");
    }
    if (udpStarted) {
        udpServer.stop(); 
        udpStarted = false;
    }
  }

  void handle() {
    if (WiFi.status() != WL_CONNECTED && WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA) {
        return; 
    }

    if (!serverStarted) {
        Serial.println("SerialSplitter: (re)start");
        server->begin();
        serverStarted = true;
    }

    // Enabling this in the past caused it to eat UDP broadcast from the master
    // hopefully UDP_COOLDOWN_MS will fix this and allow enabling safely
    if (!udpStarted) {
        Serial.println("SerialSplitter: (re)start UDP");
        if (udpServer.begin(udpPort)) {
            udpStarted = true;
        }
    }

    if (server->hasClient()) {
      if (!client || !client.connected()) {
        if (client) client.stop();
        // Fixed Core 3.x deprecation: converted available() to accept()
        client = server->accept(); 
	// Prevent 3s blocking stalls if TCP client stops reading, and disable Nagle buffering
        client.setTimeout(0);
        client.setNoDelay(true);
        client.println("\n--- Connected to ESP32 Serial Splitter ---");
      } else {
        // Fixed Core 3.x deprecation: converted available() to accept()
        WiFiClient reject = server->accept();
        reject.stop(); 
      }
    }
  }

  // --- STREAM/PRINT OUTPUT OVERRIDES ---
  
  int availableForWrite() override {
    return hw ? hw->availableForWrite() : 0;
  }

  size_t write(uint8_t c) override {
    if (hw && hw->availableForWrite() > 0) {
      hw->write(c);
    }
    if (client && client.connected()) {
      client.write(c);
    }
    return 1;
  }

  size_t write(const uint8_t *buffer, size_t size) override {
    if (!buffer || size == 0) return 0;
    if (hw && hw->availableForWrite() >= (int)size) {
       hw->write(buffer, size);
     }
     if (client && client.connected()) {
       client.write(buffer, size);
     }
     return size;
   }

  // --- STREAM INPUT OVERRIDES (TWO-WAY COMMS) ---

  int available() override { 
    int total = hw->available();
    if (client) {
      total += client.available();
    }
    if (udpStarted) {
        // RATE LIMITING: Only check for new UDP packets every 100ms
        if (millis() - lastUdpCheck > UDP_COOLDOWN_MS) {
            lastUdpCheck = millis();
            if (udpServer.available() == 0) {
                udpServer.parsePacket();
            }
        }
        total += udpServer.available();
    }
    return total;
  }

  int read() override { 
    if (client && client.available()) {
      return client.read();
    }
    if (udpStarted && udpServer.available()) {
      return udpServer.read();
    }
    return hw->read(); 
  }

  int peek() override { 
    if (client && client.available()) {
      return client.peek();
    }
    if (udpStarted && udpServer.available()) {
      return udpServer.peek();
    }
    return hw->peek(); 
  }

  void flush() override { 
    if (hw) {
      hw->flush();
    }
  }
};

// 2. Instantiate our splitter using decltype(Serial) to automatically match the architecture
GenericSerialSplitter<decltype(Serial)> Splitter(Serial, TELNET_PORT);

// 3. THE HIJACK: Replace the word "Serial" with our Splitter for the rest of this file
#undef Serial
#define Serial Splitter


// ###########################################################################################
// End of Serial hijack, SoftAP Diagnostic & DHCP Management Utilities Start
// ###########################################################################################

#include <OmEspHelpers.h>
#include <esp_wifi.h> // esp_wifi_get_ps
#include <esp_netif.h>
#include <ESPmDNS.h>
#include <mdns.h> 
#include <WiFiMulti.h>
#include <ArduinoOTA.h>

volatile uint8_t wifi_master_slave_forceap = 0;

OmWebServer *WebServer = NULL;
OmWebPages *WebPages = NULL;

// Allows a connected device via serial to feed a remote IP as a 32bit number
IPAddress RPI_IP = IPAddress(0, 0, 0, 0);
// Allows a slave ESP32 to send its IP to be notified of pattern changes
IPAddress Slave_IPs[MAX_SLAVES];
uint8_t slave_ip_count = 0;

bool WIFI_SLAVE_MODE = false;
bool WIFI_FORCEAP_MODE = false;
// is the wifi client/slave connecting to the home test debug network?
// try to connect to the ESP32 AP first
bool WIFI_CLIENT_TESTING_NETWORK = false;
volatile uint32_t send_slave_ip_to_master_tries = 0;
volatile uint32_t lastMasterBootToken = 0;   // Tracked by Slave to detect Master restarts

// UDP portable discovery of slave to find ESP32 master (which in turn sends it back change pattern commands)
#include <NetworkUdp.h>
const uint16_t UDP_DISCOVERY_PORT = 4210;
NetworkUDP udp; // Core 3.x native class type (WiFiUDP is just a typedef of this)
uint32_t masterBootToken = 0;       // Generated by Master at boot
IPAddress Master_IP(0, 0, 0, 0); // Dynamically updated by Slave

// packed is needed to ensure the compiler does not add extra space between elements
#define BEACON_STR_SIZE 12
struct __attribute__((packed)) MasterBeaconPacket {
    char beacon_id[BEACON_STR_SIZE];
    uint32_t boot_token;
};

volatile bool ota_in_progress = false;

void (*wifi_main_page_callback)() = nullptr;
void (*wifi_connect_callback)() = nullptr;

// this may stop udp packets from being processed, turn off when not debugging
// #define WIFI_SNIFFER
#ifdef WIFI_SNIFFER
    // Volatile globals to pass data safely from ISR to the main thread
    volatile bool sniffer_caught_packet = false;
    volatile uint16_t sniffer_packet_len = 0;

    void IRAM_ATTR wifi_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
        if (type != WIFI_PKT_DATA) return; 

        wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
        uint8_t *payload = pkt->payload;
        uint16_t len = pkt->rx_ctrl.sig_len;

        // Brute-force search for Port 4210 (0x1072)
        for (uint16_t i = 0; i < len - 1; i++) {
            if (payload[i] == 0x10 && payload[i+1] == 0x72) {
                // NEVER print here. Just capture the state and exit quickly.
                sniffer_packet_len = len;
                sniffer_caught_packet = true;
                break;
            }
        }
    }
#endif


// Local table to track DHCP leases since ESP-IDF v5.1 hides the lwIP table
struct APClient {
    uint8_t mac[6];
    uint32_t ip;
    bool active;
};
APClient ap_clients[16];

void onApEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
    case ARDUINO_EVENT_WIFI_AP_START:
	wifi_network_stable = true;
        // The AP interface is now fully up and running
        Serial.printf("[WIFI AP] Access Point Started successfully! IP: %s\r\n", WiFi.softAPIP().toString().c_str());
	if (wifi_connect_callback) wifi_connect_callback();
        break;

	case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
	    Serial.printf("[WIFI AP] Hardware STA Connected | MAC: %02X:%02X:%02X:%02X:%02X:%02X | AID: %d\r\n",
			  info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
			  info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
			  info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5],
			  info.wifi_ap_staconnected.aid);
	    
	    for (int i = 0; i < 16; i++) {
		if (!ap_clients[i].active || memcmp(ap_clients[i].mac, info.wifi_ap_staconnected.mac, 6) == 0) {
		    memcpy(ap_clients[i].mac, info.wifi_ap_staconnected.mac, 6);
		    ap_clients[i].active = true;
		    if (!ap_clients[i].ip) ap_clients[i].ip = 0; 
		    break;
		}
	    }
	    break;

	case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: {
	    const uint8_t *raw_ip = (const uint8_t*)&info.wifi_ap_staipassigned.ip.addr;
	    Serial.printf("[DHCP SERVER] Lease Assigned | IP: %d.%d.%d.%d\r\n",
			  raw_ip[0], raw_ip[1], raw_ip[2], raw_ip[3]);
	    
	    for (int i = 0; i < 16; i++) {
		if (ap_clients[i].active && memcmp(ap_clients[i].mac, info.wifi_ap_staipassigned.mac, 6) == 0) {
		    ap_clients[i].ip = info.wifi_ap_staipassigned.ip.addr;
		    break;
		}
	    }
	    break;
	}

	case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
	    Serial.printf("[WIFI AP] Hardware STA Disconnected | MAC: %02X:%02X:%02X:%02X:%02X:%02X | AID: %d\r\n",
			  info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
			  info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
			  info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5],
			  info.wifi_ap_stadisconnected.aid);
	    
	    for (int i = 0; i < 16; i++) {
		if (ap_clients[i].active && memcmp(ap_clients[i].mac, info.wifi_ap_stadisconnected.mac, 6) == 0) {
		    ap_clients[i].active = false;
		    ap_clients[i].ip = 0;
		    break;
		}
	    }
	    break;

	default:
	    break;
    }
}

void configure_short_dhcp_lease() {
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
	esp_netif_dhcps_stop(ap_netif);
	
	uint32_t lease_time = 20; 
	esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_IP_ADDRESS_LEASE_TIME, &lease_time, sizeof(lease_time));
	
	esp_netif_dhcps_start(ap_netif);
	Serial.println(">>>>>>>>>> [DHCP SERVER] Lease time configured to 20 minutes <<<<<<<");
    }
}

void print_ap_client_table() {
    if (WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA) return;

    wifi_sta_list_t wifi_sta_list;
    if (esp_wifi_ap_get_sta_list(&wifi_sta_list) != ESP_OK) return;

    wifi_config_t ap_conf;
    uint8_t max_sta_configured = AP_MAX_CLIENTS;
    if (esp_wifi_get_config(WIFI_IF_AP, &ap_conf) == ESP_OK) {
	if (ap_conf.ap.max_connection < AP_MAX_CLIENTS) {
	    Serial.printf("\n>>> [AUTO-HEAL] AP max_connection was %d! Re-forcing to %d...<<<<\r\n", 
			  ap_conf.ap.max_connection, AP_MAX_CLIENTS);
	    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, 1, 0, AP_MAX_CLIENTS);
	    configure_short_dhcp_lease();
	    esp_wifi_get_config(WIFI_IF_AP, &ap_conf);
	}
	max_sta_configured = ap_conf.ap.max_connection;
    }

    uint8_t dhcp_max = 8; 
    
    int active_leases = 0;
    for (int i = 0; i < 16; i++) {
	if (ap_clients[i].active && ap_clients[i].ip != 0) active_leases++;
    }

    int dhcp_slots_left = (int)dhcp_max - active_leases;
    int wifi_slots_left = (int)max_sta_configured - (int)wifi_sta_list.num;

    Serial.println("\r\n============================= AP STATUS =============================");
    Serial.printf("Wi-Fi Associated STAs : %d / %d (Slots Available: %d)\r\n",
		  wifi_sta_list.num, max_sta_configured, max(0, wifi_slots_left));
    Serial.printf("Active DHCP IP Leases : %d / %d (Leases Available: %d)\r\n",
		  active_leases, dhcp_max, max(0, dhcp_slots_left));
    Serial.println("---------------------------------------------------------------------");

    for (int i = 0; i < wifi_sta_list.num; i++) {
	wifi_sta_info_t *sta = &wifi_sta_list.sta[i];
	
	char ip_str[16] = "No IP / Pending";
	for (int j = 0; j < 16; j++) {
	    if (ap_clients[j].active && memcmp(sta->mac, ap_clients[j].mac, 6) == 0) {
		if (ap_clients[j].ip != 0) {
		    const uint8_t *raw_ip = (const uint8_t*)&ap_clients[j].ip;
		    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", raw_ip[0], raw_ip[1], raw_ip[2], raw_ip[3]);
		}
		break;
	    }
	}

	Serial.printf("[%02d] MAC: %02X:%02X:%02X:%02X:%02X:%02X | RSSI: %4d dBm | IP: %s\r\n",
		      i + 1,
		      sta->mac[0], sta->mac[1], sta->mac[2], sta->mac[3], sta->mac[4], sta->mac[5],
		      sta->rssi, ip_str);
    }
    Serial.println("=====================================================================\r\n");
}

// ---------------------------------------------------------------------------
volatile bool slave_ip_sent = false;

void send_slave_ip_to_master() {
    if (!wifi_network_stable) return;
    if (send_slave_ip_to_master_tries > 15) return;
    
    // (uint32_t)Master_IP != 0 mathematically guarantees we never 
    // suffer a TCP lockup trying to connect to an uninitialized IP object.
    if (WIFI_SLAVE_MODE && WiFi.status() == WL_CONNECTED && !slave_ip_sent) {
	if ((uint32_t)Master_IP != 0) {
	    NetworkUDP checkinUdp; // Use a local, transient UDP socket

	    if (checkinUdp.beginPacket(Master_IP, 23)) {
		IPAddress myIP = WiFi.localIP();
		
		
		uint32_t ipNum = ((uint32_t)myIP[0] << 24) | 
				 ((uint32_t)myIP[1] << 16) | 
				 ((uint32_t)myIP[2] << 8)  | 
				  (uint32_t)myIP[3];
				  
		uint64_t payload = (uint64_t)ipNum | 0x100000000ULL;
		String payloadStr = "";
		uint64_t temp = payload;
		while(temp > 0) {
		    payloadStr = (char)('0' + (temp % 10)) + payloadStr;
		    temp /= 10;
		}
		checkinUdp.println(payloadStr);
		checkinUdp.endPacket();
		slave_ip_sent = true;
		Serial.printf("Successfully sent Slave IP %s to Master at %s via UDP\r\n", payloadStr, Master_IP.toString().c_str());
	    } else {
		Serial.print("Cannot send slave IP to Master (unknown IP), will retry... ");
		Serial.println(send_slave_ip_to_master_tries);
		send_slave_ip_to_master_tries+=1;
	    }
	}
    }
}

void broadcast_master_presence() {
     if (!WIFI_SLAVE_MODE) {
         if (WiFi.status() == WL_CONNECTED || WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
             bool isAP = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
             
             IPAddress broadcastIP = isAP ? IPAddress(255, 255, 255, 255) : WiFi.broadcastIP();
             
             // Prepare the tagged payload
             MasterBeaconPacket beacon = {0};
             strncpy(beacon.beacon_id, WIFI_BEACON_ID, sizeof(beacon.beacon_id) - 1);
             beacon.boot_token = masterBootToken;
             
             if (udp.beginPacket(broadcastIP, UDP_DISCOVERY_PORT)) {
                 udp.write((uint8_t*)&beacon, sizeof(beacon));
                 udp.endPacket();
                 EVERY_N_SECONDS(60) {
                    Serial.printf(">>>>>>>>>>>>>>> Master: Sent beacon [ID: %s] %u to %s:%d", beacon.beacon_id, masterBootToken, broadcastIP.toString().c_str(), UDP_DISCOVERY_PORT);
                    if (slave_ip_count > 0) {
                        Serial.print(" (current known Slave IPs: ");
                        for(uint8_t i = 0; i < slave_ip_count; i++) {
                            Serial.print(Slave_IPs[i].toString().c_str());
                            if(i < slave_ip_count - 1) Serial.print(", ");
                        }
                        Serial.println(")");
                    } else {
                       Serial.println(" (slave IPs unknown)");
                    }
                }
             }

             // 2. The robust Unicast Rescue (Required for rebooted slaves)
             for (uint8_t i = 0; i < slave_ip_count; i++) {
                 if (udp.beginPacket(Slave_IPs[i], UDP_DISCOVERY_PORT)) {
                     udp.write((uint8_t*)&beacon, sizeof(beacon));
                     udp.endPacket();
                 }
             }
         }
     }
}

void rebuild_main_page() {
    static uint8_t count=0;
    count++;

    WebPages->beginPage("Main");

    WebPages->addHtml([] (OmXmlWriter & w, int ref1, void *ref2)
    {
	// Keep track of how many times the web page is rebuilt
        w.puts(WIFI_SLAVE_MODE ? "Slave" : WIFI_FORCEAP_MODE ? "ForceAP" : "Master ");
	w.puts((String(" Built: ") + __DATE__ + " " + __TIME__ ).c_str());
	w.puts(". Html Refresh: ");
	w.puts(String(count).c_str());
	w.puts("<BR>\n");
        if (WIFI_SLAVE_MODE) {
            w.puts("Master IP: ");
            w.puts("<a href=http://");
            w.puts(Master_IP.toString().c_str());
            w.puts("/>");
            w.puts(Master_IP.toString().c_str());
            w.puts("</a> ");
            w.puts("<BR>\n");
        } else {
            w.puts("Slave IPs: ");
            if (slave_ip_count == 0) {
                w.puts("None");
            } else {
                for(uint8_t i = 0; i < slave_ip_count; i++) {
                    w.puts("<a href=http://");
                    w.puts(Slave_IPs[i].toString().c_str());
                    w.puts("/>");
                    w.puts(Slave_IPs[i].toString().c_str());
                    w.puts("</a> ");
                }
            }
            w.puts("<BR>\n");
        }
    });
    if (wifi_main_page_callback) wifi_main_page_callback();
}


void listen_for_master_broadcast() {
    if (!wifi_network_stable) return;
    if (WIFI_SLAVE_MODE) {
        int packetSize;

        #ifdef WIFI_SNIFFER
            if (sniffer_caught_packet) {
                Serial.printf("\r\n[TCPDUMP] HARDWARE MAC RX: Port 4210 frame hit the antenna! (Len: %d)\r\n", sniffer_packet_len);
                sniffer_caught_packet = false;
            }
        #endif

        if (WiFi.status() == WL_CONNECTED) {
            static bool udpActive = false;
            if (!udpActive) {
                udp.begin(UDP_DISCOVERY_PORT);
                udpActive = true;
                Serial.printf(">>>>>>>>>>>>>  Slave Discovery reliably bound to port %d, listening for %s\r\n", UDP_DISCOVERY_PORT, WIFI_BEACON_ID);
            }

            packetSize = udp.parsePacket();
            if (packetSize >= sizeof(MasterBeaconPacket)) {
                IPAddress senderIP = udp.remoteIP();

                MasterBeaconPacket beacon;
                udp.read((uint8_t*)&beacon, sizeof(beacon));
		beacon.beacon_id[BEACON_STR_SIZE]=0;
                uint32_t receivedToken = beacon.boot_token;
                
                // Reject packets that do not match our defined network ID
                if (strncmp(beacon.beacon_id, WIFI_BEACON_ID, sizeof(beacon.beacon_id)) != 0) {
		    Serial.printf("!!!!!>>>>>>>>>>>>>  Slave ignoring beacon %s != %s from %s\r\n", beacon.beacon_id , WIFI_BEACON_ID, senderIP.toString().c_str());
                    return; 
                }
                
                if (senderIP != Master_IP || receivedToken != lastMasterBootToken) {
                    Serial.printf(">>>>>>>>>>>>>> Master Found! IP: %s, Token: %u, ID: %s\r\n", senderIP.toString().c_str(), receivedToken, beacon.beacon_id);
                    Master_IP = senderIP;
                    rebuild_main_page();
                    lastMasterBootToken = receivedToken;
                    slave_ip_sent = false; 
                    send_slave_ip_to_master_tries = 0; 
                    
                    #ifdef WIFINOSLEEP
                      Serial.println("Now that we know master, enable WIFI Sleep to save power");
                      WiFi.setSleep(true);
                      esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
                    #endif
                } else if (!slave_ip_sent) {
                    send_slave_ip_to_master_tries = 0;
                    Serial.printf(">>>>>>>>>>>>>> Need to send IP to Master %s\r\n", senderIP.toString().c_str());
                }
            }
        }
    }
}

void send_to_slaves(const char *str) {
	if (slave_ip_count > 0) {
	//#define MS_TCP_UPDATES
	#ifdef MS_TCP_UPDATES
	    uint8_t last_idx = slave_ip_count - 1;
	    WiFiClient slaveClient;
	    if (slaveClient.connect(Slave_IPs[last_idx], 23, 200)) {
		Serial.printf("Sending %s to %s via TCP\n\r", str, Slave_IPs[last_idx].toString().c_str());
		// Send the raw ASCII pattern number. The slave's IR_Serial_Handler 
		// loop will pick it up as digits and load it into new_pattern
		slaveClient.print(str);
		slaveClient.clear();
		// Give enough time for the full data to go through before we tear down the connection
		delay(300);
		slaveClient.stop();
	    }
	#else
	    NetworkUDP udpMaster;
	    for (uint8_t i = 0; i < slave_ip_count; i++) {
		if (udpMaster.beginPacket(Slave_IPs[i], 23)) {
		    Serial.printf("Sending %s to %s via UDP\n\r", str, Slave_IPs[i].toString().c_str());
		    udpMaster.println(str); // Send the pattern digits with a newline delimiter
		    udpMaster.endPacket();
		}
	    }
	#endif
    } else {
	Serial.printf("Wifi Master, but slave unknown, so cannot send pattern change\n\r");
    }
}

uint8_t parse_new_IP(uint64_t received_pattern) {
    uint32_t ip_part = (uint32_t)(received_pattern & 0xFFFFFFFF);
    uint8_t octet1 = (ip_part & 0xFF000000) / 0x1000000;
    uint8_t octet2 = (ip_part & 0x00FF0000) / 0x10000;
    uint8_t octet3 = (ip_part & 0x0000FF00) / 0x100;
    uint8_t octet4 = (ip_part & 0x000000FF) / 1;

    if (received_pattern >= 0x100000000ULL) {
	// Clear the 3rd bit to revert back to 192.x.x.x
	octet1 &= ~0x20; 
	IPAddress incoming_ip(octet1, octet2, octet3, octet4);
	
	bool known = false;
	for (uint8_t i = 0; i < slave_ip_count; i++) {
	    if (Slave_IPs[i] == incoming_ip) {
		known = true;
		break;
	    }
	}
	
	if (!known) {
	    if (slave_ip_count < MAX_SLAVES) {
		Slave_IPs[slave_ip_count++] = incoming_ip;
	    } else {
		// Shift array left to enforce FIFO limit, dropping oldest IP
		for (uint8_t i = 1; i < MAX_SLAVES; i++) {
		    Slave_IPs[i - 1] = Slave_IPs[i];
		}
		Slave_IPs[MAX_SLAVES - 1] = incoming_ip;
	    }
	    Serial.print(">>>>>>>>>>>>>>>> ESP32 Master received and stored new Slave IP: ");
	    Serial.println(incoming_ip.toString().c_str());
	}
	return 1;
    }
    return 0;
}





// ###########################################################################################
// End of Network Tools, Wifi/OTA Start
// ###########################################################################################

// Prepend Master/Slave or nothing to some debug messages
void MSF_Print() {
    if (WIFI_SLAVE_MODE) Serial.print("Slave ");
    else if (WIFI_FORCEAP_MODE) Serial.print("ForceAP  ");
    else Serial.print("Master ");
}

#if 0
How do you find your device after it boots?

sauron:~# avahi-browse -rt _arduino._tcp
+  wlan0 IPv4 P1.825-64BY64                                 _arduino._tcp        local
+  wlan0 IPv4 Marc NFL-3                                    _arduino._tcp        local
+  wlan0 IPv4 P4-64BY32X2                                   _arduino._tcp        local
+  wlan0 IPv4 Marc NFL                                      _arduino._tcp        local
=  wlan0 IPv4 P1.825-64BY64                                 _arduino._tcp        local
   hostname = [P1\.825-64BY64.local]
   address = [192.168.205.187]
   port = [3232]
   txt = ["board=adafruit_matrixportal_esp32s3" "tcp_check=no" "ssh_upload=no" "auth_upload=no"]
=  wlan0 IPv4 Marc NFL-3                                    _arduino._tcp        local
   hostname = [Marc\032NFL-3.local]
   address = [192.168.205.194]
   port = [3232]
   txt = ["board=waveshare_esp32_s3_zero" "tcp_check=no" "ssh_upload=no" "auth_upload=no"]
=  wlan0 IPv4 P4-64BY32X2                                   _arduino._tcp        local
   hostname = [P4-64BY32X2.local]
   address = [192.168.205.203]
   port = [3232]
   txt = ["board=adafruit_matrixportal_esp32s3" "tcp_check=no" "ssh_upload=no" "auth_upload=no"]
=  wlan0 IPv4 Marc NFL                                      _arduino._tcp        local
   hostname = [Marc\032NFL.local]
   address = [192.168.205.218]
   port = [3232]
   txt = ["board=waveshare_esp32_s3_zero" "tcp_check=no" "ssh_upload=no" "auth_upload=no"]
#endif

// Standalone TCP listener for port 3232 on the AP interface (Used only for AP mDNS discovery)
static WiFiServer ota_tcp_listener(3232);

// merlin:~# avahi-browse -rt _arduino._tcp -d local
// +  wlan0 IPv4 Marc NFL NOFLASH Slave Oct 27 2026 21:41:48   _arduino._tcp        local
// +  wlan0 IPv4 Marc NFL NOFLASH Slave Oct 27 2026 21:41:48-3 _arduino._tcp        local
// +  wlan0 IPv4 NOFLASH-Marc-NFL-ForceAP-Oct-30-2026-12-51-48 _arduino._tcp        local
// =  wlan0 IPv4 Marc NFL NOFLASH Slave Oct 27 2026 21:41:48   _arduino._tcp        local
// hostname = [Marc\032NFL\032NOFLASH\032Slave\032Oct\03227\0322026\03221\05841\05848.local]
// address = [192.168.4.3]
// port = [3232]
// txt = ["board=waveshare_esp32_s3_zero" "tcp_check=no" "ssh_upload=no" "auth_upload=yes"]
// wlan0 IPv4 Marc NFL NOFLASH Slave Oct 27 2026 21:41:48-3 _arduino._tcp        local
// hostname = [Marc\032NFL\032NOFLASH\032Slave\032Oct\03227\0322026\03221\05841\05848-3.local]
// address = [192.168.4.4]
// port = [3232]
// txt = ["board=waveshare_esp32_s3_zero" "tcp_check=no" "ssh_upload=no" "auth_upload=yes"]
// wlan0 IPv4 NOFLASH-Marc-NFL-ForceAP-Oct-30-2026-12-51-48 _arduino._tcp        local
// hostname = [NOFLASH-Marc-NFL-ForceAP-Oct-30-2026-12-51-48.local]
// address = [192.168.4.1]
// port = [3232]
// txt = ["board=waveshare_esp32_s3_zero" "auth_upload=no"]

// Note that this does not work if the ESP32 is the AP
void restart_ota_service() {
    if (WiFi.getMode() == WIFI_MODE_NULL || WiFi.getMode() == WIFI_OFF) {
        Serial.println("[OTA] Network not ready, aborting OTA restart.");
        return;
    }

    ArduinoOTA.end();
    MDNS.end();
    ota_tcp_listener.end();
    // 1. Sanitize hostname to strict RFC alphanumeric + hyphen format
    String clean_date = String(__DATE__);
    while (clean_date.indexOf("  ") >= 0) {
        clean_date.replace("  ", " ");
    }
    clean_date.replace(" ", "-");
    String clean_time = String(__TIME__);
    clean_time.replace(":", "-");

    String ota_name = get_hostname() + "-" +
                     (WIFI_SLAVE_MODE ? "Slave-" : WIFI_FORCEAP_MODE ? "ForceAP-" : "Master-") +
                     clean_date + "-" + clean_time;

    if (ESP.getFlashChipSize() <= 5000000) {
        ota_name = "NOFLASH-" + ota_name;
    }

    // Automatically extract the board name from the Arduino core compiler flags
    #ifdef ARDUINO_VARIANT
        const char* current_board_name = ARDUINO_VARIANT;
    #else
        const char* current_board_name = "esp32_unknown";
    #endif

    // 2. Initialize MDNS explicitly before registering services
    if (!MDNS.begin(ota_name.c_str())) {
        Serial.println("[mDNS] Error setting up MDNS responder!");
    }

    // 3. Branch logic based on active interface
    if (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA) {
        // --- CLIENT MODE (SLAVE) ---
        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif) {
            mdns_netif_action(sta_netif, MDNS_EVENT_ENABLE_IP4);
        }

        // ArduinoOTA handles its own mDNS cleanly on the STA interface
        ArduinoOTA.setHostname(ota_name.c_str());
        ArduinoOTA.setPassword(WIFI_OTA_PWD);
        ArduinoOTA.begin();

        // Explicitly advertise _arduino._tcp on MDNS so avahi and IDE find it immediately
        //MDNS.addService("arduino", "tcp", 3232);
        // ArduinoOTA already adds the service. Only append custom TXT records.
        MDNS.addServiceTxt("arduino", "tcp", "board", current_board_name);
        MDNS.addServiceTxt("arduino", "tcp", "auth_upload", "yes");
        Serial.printf(">>>>> [OTA] Client STA Broadcast active: %s.local:3232 <<<<<\r\n", ota_name.c_str());
    } else {
        // --- ACCESS POINT MODE (MASTER / FORCEAP) ---
        // CRITICAL ESP-IDF FIX: Force mDNS to bind to the SoftAP netif handle for IPv4.
        // Without this native IDF call, SoftAP OTA discovery fails on most devices.
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap_netif) {
            mdns_netif_action(ap_netif, MDNS_EVENT_ENABLE_IP4);
            Serial.println("[mDNS] Successfully attached to WIFI_AP_DEF");
        }

        // Advertise the standard Arduino OTA service
        MDNS.addService("arduino", "tcp", 3232);
        MDNS.addServiceTxt("arduino", "tcp", "board", current_board_name);
        MDNS.addServiceTxt("arduino", "tcp", "auth_upload", "no");
        Serial.printf("[mDNS] Advertising _arduino._tcp on %s.local (Board: %s)\r\n", ota_name.c_str(), current_board_name);
	// Open TCP 3232 listener directly on the AP interface to satisfy avahi-browse / nc -zv
	ota_tcp_listener.begin();
	Serial.printf(">>>>> [OTA] AP Dummy Listener & mDNS active on 192.168.4.1:3232 (%s.local) <<<<<\r\n", ota_name.c_str());
    }
}

void connectionStatus(const char *ssid, bool trying, bool failure, bool success)
{
  static uint8_t failure_cnt = 0;
  static bool ap = WIFI_FORCEAP_MODE? true: false;
  const char *what = "?";

  if (trying)       { 
      what = "trying"; 
      wifi_network_stable = false;
      Splitter.restart(); // CRITICAL: Stop UDP poll on interface drop
  }
  else if (failure) { 
      what = "failure"; 
      failure_cnt++; 
      slave_ip_sent = false; // Reset so the slave retries if WiFi drops
      wifi_network_stable = false;
      Splitter.restart(); // CRITICAL: Stop UDP poll on interface drop
  } else if (success) { 
      what = "success"; 
      failure_cnt = 0; 
      send_slave_ip_to_master_tries = 0;
      slave_ip_sent = false;      // Forces execution update on local drop
      lastMasterBootToken = 0;    // Forces a fresh sync check via UDP stream
      wifi_network_stable = true;
      Splitter.restart();
      Serial.printf("|Started wifi, slave mode enabled? %d\n\r", WIFI_SLAVE_MODE);

      restart_ota_service();
  }
  // Disable WiFi power save to prevent LwIP from dropping broadcasts between DTIM windows 
  // May add around 200mW of power
#ifdef WIFINOSLEEP
  WiFi.setSleep(false);
  // do it both ways
  esp_wifi_set_ps(WIFI_PS_NONE);
#endif
  wifi_ps_type_t ps_type = WIFI_PS_MAX_MODEM;
  esp_wifi_get_ps(&ps_type);

  // 0 = WIFI_PS_NONE (disabled), 1 = WIFI_PS_MIN_MODEM, 2 = WIFI_PS_MAX_MODEM
  MSF_Print();
  Serial.printf("%s: connectionStatus for '%s' is now '%s' fail cnt: %d, power save: %d\n\r", __func__, ssid, what, failure_cnt, ps_type);

  if (!ap and failure_cnt > 1) {
      // For the wifi slave, allow switching forever between AP and home test network until one works
      if (WIFI_SLAVE_MODE) {
          WebServer->clearWifis();
          WiFi.disconnect();
          failure_cnt = 0;
          if (!WIFI_CLIENT_TESTING_NETWORK) {
              Serial.println(">>>>>>>>>>>>>>>>>>>> Client/Slave mode: Too many failures with AP " WIFI_AP_SSID ", switching to " WIFI_SSID);
              WebServer->addWifi(WIFI_SSID, WIFI_PASSWORD); // home test network
              WIFI_CLIENT_TESTING_NETWORK = true;
          } else {
              Serial.println(">>>>>>>>>>>>>>>>>>>> Client/Slave mode: Too many failures with test network " WIFI_SSID ", switching to AP " WIFI_AP_SSID);
              WebServer->addWifi(WIFI_AP_SSID, WIFI_AP_PASSWORD); // Back to AP network
              WIFI_CLIENT_TESTING_NETWORK = false;

          }
      // Switch to AP mode after 2 attempts to connect to the test network as slave
      } else { 
          Serial.println(">>>>>>>>>>>>>>>>>>>> Too many failures setting up Wifi client to " WIFI_SSID ", switching to Wifi AP mode to " WIFI_AP_SSID);
          ap = true;
          WebServer->clearWifis();
          WiFi.disconnect();

          // Note this forces a 4 client limit, we override in AUTO-HEAL callback
          WebServer->setAccessPoint(WIFI_AP_SSID, WIFI_AP_PASSWORD);

          // AP is up! Restart Splitter so it binds to the new AP interface (192.168.4.1)
          Splitter.restart();
          failure_cnt = 0;

          restart_ota_service();
      }
  }
}

void set_wifi_mode(uint8_t msf) {
  if (msf == 1) {
      Serial.println(">>>>>>>>>>>>>> Waveshare Configured for Wifi client (slave) mode");
      WIFI_SLAVE_MODE = true;
  }
  if (msf == 2) {
      Serial.println(">>>>>>>>>>>>>> Waveshare Configured for Wifi AP mode but will try WIFI_SSID first");
      Serial.println(">>>>>>>>>>>>>> AP/Slave may not work on some APs unless you configure proxy ARP on the AP");
      WIFI_SLAVE_MODE = false;
  }
  if (msf == 3) {
      Serial.println(">>>>>>>>>>>>>> Waveshare Configured for Wifi ForceAP");
      WIFI_SLAVE_MODE = false;
      WIFI_FORCEAP_MODE = true;
  }
}

// we need a callback because it calls the matrix we can't access here
void wifi_setup(void (*main_page_callback)() = nullptr, void (*ota_start_callback)() = nullptr, void (*_wifi_connect_callback)() = nullptr) {
  wifi_main_page_callback = main_page_callback;
  wifi_connect_callback = _wifi_connect_callback;
  masterBootToken = esp_random();

  Serial.println();
  Serial.println(">>>> WIFI Setup, reading bootup config...");

  // Some chips do not have NVRAM
  #if defined(NVRAM)
      wifi_master_slave_forceap = get_nvram_master_slave_forceap();
      set_wifi_mode(wifi_master_slave_forceap);
  #endif

  // We can use I/O pins and wires to set default config
  if (!wifi_master_slave_forceap) {
    #ifdef ARDUINO_WAVESHARE_ESP32_S3_ZERO
      pinMode(WAVESHARE_MASTER_PIN, INPUT_PULLDOWN);  // top left, client/slave mode
      pinMode(WAVESHARE_FORCEAP_PIN, INPUT_PULLDOWN);
      pinMode(8, OUTPUT); // used as voltage source for wiring easiness
      digitalWrite(8, HIGH);

      if (!digitalRead(WAVESHARE_MASTER_PIN)) {
	  Serial.println(">>>>>>>>>>>>>> Waveshare Configured for Wifi client (slave) mode");
	  WIFI_SLAVE_MODE = true;
      } else if (digitalRead(WAVESHARE_FORCEAP_PIN)) {
	  Serial.println(">>>>>>>>>>>>>> Waveshare Configured for Wifi ForceAP");
	  WIFI_SLAVE_MODE = false;
	  WIFI_FORCEAP_MODE = true;
      } else { 
	  Serial.println(">>>>>>>>>>>>>> Waveshare Configured for Wifi AP mode but will try WIFI_SSID first");
	  Serial.println(">>>>>>>>>>>>>> AP/Slave may not work on some APs unless you configure proxy ARP on the AP");
	  WIFI_SLAVE_MODE = false;
      }
    // and if nothing is done, we default as slave
    #else
      Serial.println(">>>>>>>>>>>>>> Waveshare Configured for Wifi client (slave) mode");
      WIFI_SLAVE_MODE = true;
    #endif
  }

  Serial.println(">>>> OmWebserver Setup...");
  WebServer = new OmWebServer();
  WebServer->setStatusCallback(connectionStatus);

  WebPages = new OmWebPages();
  WebPages->setBuildDateAndTime(__DATE__, __TIME__);

  // To get the ordering we want, we start with a dummy page
  // that gets replaced with the full page later.
  WebPages->beginPage("Main");
  WebPages->addHtml([] (OmXmlWriter & w, int ref1, void *ref2)
  {
      w.putf("Placeholder\n");
  });

  WebServer->setHandler(*WebPages);
  rebuild_main_page();

  // Register debug/status callbacks
  WiFi.onEvent(onApEvent);

  // Wi-Fi Event Callbacks
  WiFi.onEvent([_wifi_connect_callback](WiFiEvent_t event, WiFiEventInfo_t info) {
    Serial.printf("\n[WiFi] Connected! IP: %s | Hostname: %s.local | Telnet ready port %d (use nc, not telnet)\n",
                  WiFi.localIP().toString().c_str(),
                  get_hostname().c_str(), TELNET_PORT);

		  slave_ip_sent = false;
		  send_slave_ip_to_master_tries = 0;
		  send_slave_ip_to_master();
		  if (_wifi_connect_callback) _wifi_connect_callback();
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    uint8_t reason = info.wifi_sta_disconnected.reason;
    Serial.printf("\n[WiFi] Disconnected. Reason: %d\n", reason);
    
    // Force a reconnect if the driver gives up (e.g., AUTH_FAIL or NO_AP_FOUND)
    if (reason == 202 || reason == 201 || reason == 4) {
        Serial.println("[WiFi] Forcing manual reconnect...");
        // Use WiFi.begin rather than WiFi.reconnect() to fully reset the station state
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // Are we using the fallback testing network (by default we use the AP made by the master)
  if (WIFI_SLAVE_MODE) {
      // try to connect to the ESP32 AP first
      WIFI_CLIENT_TESTING_NETWORK = false;
      Serial.println(">>>>>>>>>>>>>> Wifi slave mode, will try to connect to AP " WIFI_AP_SSID);
      WebServer->addWifi(WIFI_AP_SSID, WIFI_AP_PASSWORD); // MarcLED
  } else if (WIFI_FORCEAP_MODE) { 
    // Note this forces a 4 client limit, we override in AUTO-HEAL callback
      WebServer->setAccessPoint(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  } else { 
      // Start as a client for home network, if that fails, connectionStatus() will take
      // the next step
      Serial.println(">>>>>>>>>>>>>> Wifi Configured for Wifi AP mode, will try to connect to " WIFI_SSID);
      WebServer->addWifi(WIFI_SSID, WIFI_PASSWORD);
  }

  // I think this is default now?
  WiFi.setAutoReconnect(true);

  // Enforce 10 clients immediately now that OmWebServer has likely settled
  if (!WIFI_SLAVE_MODE && (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA || WIFI_FORCEAP_MODE)) {
      Serial.printf("\n>>>>>>>>>> Post-Init: Enforcing %d AP Clients & 20m DHCP Lease <<<<<<<<<<\n", AP_MAX_CLIENTS);
      WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, AP_MAX_CLIENTS);
      configure_short_dhcp_lease();
  }

  ArduinoOTA.onStart([ota_start_callback]() {
    Serial.println("\r\n[OTA] Update started. Freeing network resources and locking loop.");
    ota_in_progress = true;
    
    // 1. Disable Wi-Fi sleep to prevent TCP dropped packets during flash erase CPU stalls
    WiFi.setSleep(false);
    
    // 2. Kill the Telnet server and all clients to free LwIP TCP memory buffers
    // But let's not doo that, it's nice to be able to watch the OTA being received over serial
    //Splitter.restart();

    // Execute the injected matrix code from the main file
    if (ota_start_callback) ota_start_callback();
  });

  ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("[OTA] Error[%u] - Transfer failed. Releasing loop lock.\r\n", error);
      ota_in_progress = false; 
      //WiFi.setSleep(true); // Restore default Wi-Fi power save
  });

  ArduinoOTA.onEnd([]() {
      // kill the telnet connection since we're about to reboot
      Splitter.restart();
      Serial.println("\r\n[OTA] Transfer Complete.");
      ota_in_progress = false;
  });
}

void wifi_loop() {
  ArduinoOTA.handle();
  if (ota_in_progress) return;

  Splitter.handle();
  WebServer->tick();

  listen_for_master_broadcast(); // Slave reads this continuously

  // If DHCP ever changes our IP while connected, immediately notify Master
  if (WIFI_SLAVE_MODE && wifi_network_stable && WiFi.status() == WL_CONNECTED) {
      static IPAddress lastKnownLocalIP(0, 0, 0, 0);
      IPAddress currentIP = WiFi.localIP();
      if (currentIP != lastKnownLocalIP && (uint32_t)currentIP != 0) {
          lastKnownLocalIP = currentIP;
          slave_ip_sent = false;
          send_slave_ip_to_master_tries = 0;
          send_slave_ip_to_master();
      }
  }
  
  EVERY_N_SECONDS(5) {
      broadcast_master_presence(); // Master broadcasts presence
      send_slave_ip_to_master();
  }
  
  EVERY_N_SECONDS(30) {
      if (!WIFI_SLAVE_MODE) {
  	print_ap_client_table();
      }
  }
  
  EVERY_N_SECONDS(60) {
    wifi_ps_type_t ps_type = WIFI_PS_MAX_MODEM;
    esp_wifi_get_ps(&ps_type);
    MSF_Print();
    // 8K gives Loop() - Free Stack Space: 1952
    // Debug exception reason: Stack canary watchpoint triggered (loopTask) 
    // After changing stack size to 16KB, I hae now 7916KB free
    // 0 = WIFI_PS_NONE (disabled), 1 = WIFI_PS_MIN_MODEM, 2 = WIFI_PS_MAX_MODEM
    Serial.printf("Loop() - Stack: %d, BSSID: %s, Ch: %d, RSSI: %d, PS: %d ",
        uxTaskGetStackHighWaterMark(NULL), WiFi.BSSIDstr().c_str(),
        WiFi.channel(), WiFi.RSSI(), ps_type);
      wifi_mode_t mode = WiFi.getMode();

    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("SSID: ");
        Serial.println(WiFi.SSID());
      } else {
        Serial.println("STA (Client): Disconnected");
      }
    }
    else if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
      Serial.print("AP SSID: ");
      Serial.println(WiFi.softAPSSID());
    }
    else if (mode == WIFI_MODE_NULL) {
      Serial.println("Wi-Fi off");
    }
    // Keep Master's LwIP ARP entry for this Slave from expiring (ARP_MAXAGE = 300s)
    if (WIFI_SLAVE_MODE && wifi_network_stable && (uint32_t)Master_IP != 0) {
        slave_ip_sent = false;
        send_slave_ip_to_master_tries = 0;
    }
  }
}

// ####################################################################
// NVRAM Storage, please define the NVRAM Storage Name 
// ####################################################################

// From neomatrix_config.h
extern uint8_t matrix_brightness;

#ifdef ESP32
#define NVRAM
#endif

#ifdef NVRAM
  #ifndef NVRAM_BUCKET
  #error "#define NVRAM_BUCKET before including this file"
  #endif
  
  #include <Preferences.h>
  Preferences prefs;
  
  void set_hostname(const String& new_id) {
      prefs.begin(NVRAM_BUCKET, false); // Open namespace, false = read/write
      
      // Truncate to 16 chars
      String truncated = new_id.substring(0, 16);
      
      // Write directly using a key. No offsets needed.
      prefs.putString("hostname", truncated);
      prefs.end();
  }
  
  String get_hostname() {
      prefs.begin(NVRAM_BUCKET, true); // true = read-only
      
      // Read the key. If it doesn't exist, return empty string
      String stored_id = prefs.getString("hostname", "");
      prefs.end();
      
      if (stored_id.length() > 0) {
          return stored_id;
      }
  
      // Fallback to eFuse MAC
      uint64_t chip_mac = ESP.getEfuseMac();
      char default_id[13];
      snprintf(default_id, sizeof(default_id), "%012llX", (unsigned long long)chip_mac);
      return String(default_id);
  }
  
  void set_nvram_brightness(const uint8_t brightness) {
      prefs.begin(NVRAM_BUCKET, false); // Open namespace, false = read/write
      
      Serial.printf("%%%%%%%%%% saving brightness %d\r\n", brightness);
      prefs.putUChar("brightness", brightness);
      prefs.end();
  }
  
  uint8_t get_nvram_brightness() {
      prefs.begin(NVRAM_BUCKET, true); // true = read-only
      
      // Read the key. The second argument (64) is the default value 
      // returned if the key does not exist yet (e.g., first boot).
      uint8_t brightness = prefs.getUChar("brightness", 64);
      
      prefs.end();
      
      return brightness;
  }
  
  void set_nvram_master_slave_forceap(const uint8_t master_slave_forceap) {
      prefs.begin(NVRAM_BUCKET, false); // Open namespace, false = read/write
      
      // key length must be 15 chars or less
      Serial.printf("%%%%%%%%%% saving msforceap %d\r\n", master_slave_forceap);
      prefs.putUChar("msforceap", master_slave_forceap);
      prefs.end();
  }
  
  uint8_t get_nvram_master_slave_forceap() {
      prefs.begin(NVRAM_BUCKET, true); // true = read-only
      
      // Read the key. The second argument (0) is the default value 
      // returned if the key does not exist yet (e.g., first boot).
      uint8_t master_slave_forceap = prefs.getUChar("msforceap", 0);
      
      prefs.end();
      
      return master_slave_forceap;
  }

  void process_nvram_serial(char readchar) {
  
    if (readchar == 'w') {
      Serial.read(); // read potential newline
      // defined and read in wifi_setup
      Serial.printf("Current Wifi Mode: %d\r\n", wifi_master_slave_forceap);
      Serial.print("Input 1 (slave), 2 (master), 3 (forceap): ");
  
      // Block efficiently until a newline arrives, building the string manually
      while (true) {
        if (Serial.available()) {
          char c = Serial.read();
          if (c < '0' || c > '3') { Serial.printf("%c invalid\r\n", c); break; }
          wifi_master_slave_forceap = c - '0';
  	set_wifi_mode(wifi_master_slave_forceap);
          Serial.printf("Saving Wifi Mode %d to NVRAM\n\r", wifi_master_slave_forceap);
  	set_nvram_master_slave_forceap(wifi_master_slave_forceap);
        } else {
  	  wifi_loop();
        }
      }
    } else if (readchar == 'B') {
      Serial.printf("Saving Current Brightness To Flash: %d\r\n", matrix_brightness);
      set_nvram_brightness(matrix_brightness);
    }
    else if (readchar == 'H') {
      Serial.read(); // read potential newline
      Serial.printf("Current Hostname: %s\r\n", get_hostname().c_str());
      Serial.print("Input New Hostname: ");
  
      String newhostname = "";
      // Block efficiently until a newline arrives, building the string manually
      while (true) {
        if (Serial.available()) {
          char c = Serial.read();
          if (c == '\n') break; // End of input
          if (c == '\r') break; // End of input
  	// make hostnames compliant for networking/DNS
          if (c == ' ') c = '-';
          if (c == '_') c = '-';
          newhostname += c;
        } else {
  	// Serial can come from wifi, so allow it to run
  	wifi_loop();
        }
      }
  
      newhostname.trim(); // Clean up trailing '\r' if present
      Serial.printf("Received new hostname %s, saving in flash\r\n", newhostname.c_str());
      set_hostname(newhostname);
      Serial.println("Track hostnames in linux with: avahi-browse -rt _arduino._tcp");
      ArduinoOTA.end();
      ArduinoOTA.setHostname(newhostname.c_str());
      ArduinoOTA.begin();
    }
  }
#endif // NVRAM

#endif // WIFI_SERIAL_OTA_H
