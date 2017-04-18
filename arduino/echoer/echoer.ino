
#include <ESP8266WiFi.h>
#include <list>

extern "C"
{
#include "user_interface.h"
#include "lwip/init.h" // lwip version
}

using namespace std;

#define PORT 6969       // spin round
#define INFO_MS 10000   // 10000=10sec
#define BUFSIZE 4000

const char* ssid = "***";
const char* password = "***";

WiFiServer server(PORT);
list<WiFiClient> clients;
unsigned long nextinfo_ms;
uint8_t buf [BUFSIZE];

void info ()
{
        IPAddress ip = WiFi.localIP();
        uint8_t mac[6];
        wifi_get_macaddr(STATION_IF, mac);
        Serial.printf("ip=%i.%i.%i.%i baud=%i heap=%uB "
                 "flashsize=%iKb WND=%i MSS=%i "
                 "lwIP-%d.%d.%d sdk-%s "
                 "sta=%02x%02x%02x%02x%02x%02x "
                 "\n",
                ip[0], ip[1], ip[2], ip[3],
                Serial.baudRate(),
                ESP.getFreeHeap(),
                ESP.getFlashChipRealSize() >> 10,
                TCP_WND, TCP_MSS,
                LWIP_VERSION_MAJOR, LWIP_VERSION_MINOR, LWIP_VERSION_REVISION,
                ESP.getSdkVersion(),
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void setup ()
{
        Serial.begin(115200);

        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED)
                delay(500);
        Serial.print("\nConnected to: ");
        Serial.print(WiFi.SSID());
        Serial.print(", IP address: ");
        Serial.println(WiFi.localIP());
  
        server.begin();
        Serial.printf("\nI will repeat whatever comes to port %d\n"
                "test me with https://github.com/d-a-v/echotester\n",
                PORT);
        info();
        nextinfo_ms = millis() + INFO_MS;
}


void loop ()
{
        for (auto cli = clients.begin(); cli != clients.end(); )
        {
                if (!*cli || !cli->connected())
                {
                        if (*cli)
                                cli->stop();
                        auto dead = cli++;
                        clients.erase(dead);
                        Serial.println("client vanished");
                        info();
                        continue;
                }

                size_t ravail = cli->available();
                size_t wavail = cli->availableForWrite();
                if (ravail && wavail)
                {
                        size_t sz = ravail > wavail? wavail: ravail;
                        if (sz > BUFSIZE)
                                sz = BUFSIZE;
                        int r = cli->read(buf, sz);
                        int s = cli->write(buf, r);
                        if (r != s)
                                Serial.println(String("error: r:") + r + " s:" + s);
                }
                cli++;
        }

        if (server.hasClient())
        {
                Serial.print("new client\n");
                clients.push_back(server.available());
                //clients.rbegin()->setNoDelay(true);
                info();
        }

        if (nextinfo_ms < millis())
        {
                nextinfo_ms += INFO_MS;
                info();
        }
}

