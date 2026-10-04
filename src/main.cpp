
#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "esp_wifi.h"

#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_SCK 25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

enum Screen { HOME, SCAN, PASSWORD, CONNECTING, CSI };
Screen screenState = HOME;

String selectedSSID = "";
String passwordText = "";
int selectedNetwork = -1;
int networkCount = 0;

volatile float csiLevel = 0;
volatile uint32_t csiPackets = 0;
float baseline = 0;
bool motion = false;
uint32_t lastDraw = 0;
int graphX = 5;

struct Key {
  int x, y, w, h;
  const char *label;
};

Key keys[] = {
  {5,170,30,28,"1"},{39,170,30,28,"2"},{73,170,30,28,"3"},
  {107,170,30,28,"4"},{141,170,30,28,"5"},{175,170,30,28,"6"},
  {209,170,30,28,"7"},{243,170,30,28,"8"},{277,170,30,28,"9"},
  {5,202,30,28,"0"},{39,202,30,28,"a"},{73,202,30,28,"b"},
  {107,202,30,28,"c"},{141,202,30,28,"d"},{175,202,30,28,"e"},
  {209,202,30,28,"f"},{243,202,30,28,"g"},{277,202,30,28,"h"},
  {5,234,45,28,"OK"},{55,234,45,28,"DEL"},{105,234,65,28,"SHIFT"},
  {170,234,105,28,"SPACE"},{280,234,35,28,"<"}
};

bool hit(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx+bw && y >= by && y < by+bh;
}

void header(const char *title) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  tft.println(title);
  tft.drawFastHLine(0, 32, 320, TFT_DARKGREY);
}

void drawHome() {
  header("WIFI CSI SENSING");
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(35,70); tft.println("ESP32-CYD");
  tft.setCursor(25,105); tft.println("Wi-Fi CSI Motion");
  tft.fillRoundRect(55,150,210,50,8,TFT_BLUE);
  tft.setTextColor(TFT_WHITE); tft.setCursor(105,167); tft.println("SCAN WIFI");
}

void drawScan() {
  header("WIFI NETWORKS");
  int shown = min(networkCount, 5);
  for (int i=0;i<shown;i++) {
    int y = 45 + i*32;
    tft.fillRoundRect(5,y,310,27,4,TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    String s = String(i+1)+": "+WiFi.SSID(i);
    if (s.length() > 27) s = s.substring(0,27);
    tft.setCursor(10,y+7); tft.print(s);
    tft.setCursor(255,y+7); tft.print(WiFi.RSSI(i));
  }
  tft.fillRoundRect(10,215,140,22,4,TFT_BLUE);
  tft.fillRoundRect(170,215,140,22,4,TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(28,221); tft.print("SCAN AGAIN");
  tft.setCursor(207,221); tft.print("BACK");
}

void drawPassword() {
  header("WIFI PASSWORD");
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5,42); tft.print(selectedSSID);
  tft.drawRect(5,60,310,28,TFT_WHITE);
  String masked="";
  for (size_t i=0;i<passwordText.length();i++) masked += "*";
  tft.setCursor(10,70); tft.print(masked);
  for (auto &k: keys) {
    tft.fillRect(k.x,k.y,k.w,k.h,TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(k.x+5,k.y+9); tft.print(k.label);
  }
}

void drawCSI() {
  header("CSI MONITOR");
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(5,40); tft.print("SSID: "); tft.print(selectedSSID);
  tft.setCursor(5,54); tft.print("RSSI: "); tft.print(WiFi.RSSI()); tft.print(" dBm");
  tft.setCursor(5,68); tft.print("Packets: "); tft.print(csiPackets);
  tft.setCursor(5,82); tft.print("Motion: ");
  tft.setTextColor(motion ? TFT_RED : TFT_GREEN);
  tft.print(motion ? "YES" : "NO");
  tft.setTextColor(TFT_WHITE);
  tft.drawRect(3,98,314,100,TFT_DARKGREY);
  tft.fillRect(5,200,150,30,TFT_RED);
  tft.fillRect(165,200,150,30,TFT_DARKGREY);
  tft.setCursor(55,211); tft.print("STOP");
  tft.setCursor(215,211); tft.print("RESCAN");
}

void csiCallback(void *ctx, wifi_csi_info_t *info) {
  if (!info || !info->buf || info->len < 4) return;
  float sum=0;
  int pairs = info->len/2;
  for (int i=0;i<pairs;i++) {
    float re = info->buf[2*i];
    float im = info->buf[2*i+1];
    sum += sqrtf(re*re + im*im);
  }
  float avg = pairs ? sum/pairs : 0;
  csiLevel = 0.85f*csiLevel + 0.15f*avg;
  if (baseline == 0) baseline = csiLevel;
  baseline = 0.995f*baseline + 0.005f*csiLevel;
  motion = fabs(csiLevel-baseline) > max(4.0f, baseline*0.18f);
  csiPackets++;
}

void startCSI() {
  wifi_csi_config_t cfg = {};
  cfg.lltf_en = true;
  cfg.htltf_en = true;
  cfg.stbc_htltf2_en = true;
  cfg.ltf_merge_en = true;
  cfg.channel_filter_en = false;
  cfg.manu_scale = false;
  cfg.shift = 0;
  esp_wifi_set_csi_config(&cfg);
  esp_wifi_set_csi_rx_cb(&csiCallback, nullptr);
  esp_wifi_set_csi(true);
  csiPackets=0; csiLevel=0; baseline=0; motion=false;
}

void scanWiFi() {
  screenState = SCAN;
  header("SCANNING...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(200);
  networkCount = WiFi.scanNetworks(false, true);
  drawScan();
}

void setup() {
  Serial.begin(115200);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  tft.init();
  tft.setRotation(1);
  touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);

  WiFi.mode(WIFI_STA);
  drawHome();
}

void loop() {
  if (screenState == CSI && millis()-lastDraw > 100) {
    lastDraw=millis();
    // redraw a compact graph area
    int h = constrain((int)(csiLevel*1.5f), 1, 90);
    tft.drawFastVLine(graphX, 197-h, h, TFT_CYAN);
    graphX++;
    if (graphX > 315) {
      graphX=5;
      tft.fillRect(5,103,310,94,TFT_BLACK);
    }
    tft.fillRect(5,40,310,50,TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5,40); tft.print("SSID: "); tft.print(selectedSSID);
    tft.setCursor(5,54); tft.print("RSSI: "); tft.print(WiFi.RSSI());
    tft.setCursor(5,68); tft.print("Packets: "); tft.print(csiPackets);
    tft.setCursor(5,82); tft.print("Motion: ");
    tft.setTextColor(motion ? TFT_RED : TFT_GREEN);
    tft.print(motion ? "YES" : "NO");
  }

  if (!touch.touched()) return;
  TS_Point p = touch.getPoint();
  int x = map(p.x, 200, 3900, 0, 320);
  int y = map(p.y, 250, 3800, 0, 240);
  x=constrain(x,0,319); y=constrain(y,0,239);

  if (screenState == HOME) {
    if (hit(x,y,55,150,210,50)) scanWiFi();
  } else if (screenState == SCAN) {
    if (y >= 45 && y < 205) {
      int idx=(y-45)/32;
      if (idx < networkCount && idx < 5) {
        selectedNetwork=idx;
        selectedSSID=WiFi.SSID(idx);
        if (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN) {
          WiFi.begin(selectedSSID.c_str());
          screenState=CONNECTING;
          header("CONNECTING...");
          uint32_t t=millis();
          while (WiFi.status()!=WL_CONNECTED && millis()-t<15000) delay(100);
          if (WiFi.status()==WL_CONNECTED) {
            startCSI(); screenState=CSI; drawCSI();
          } else { drawScan(); }
        } else {
          passwordText="";
          screenState=PASSWORD; drawPassword();
        }
      }
    } else if (y>=215 && x<155) scanWiFi();
    else if (y>=215 && x>=165) { screenState=HOME; drawHome(); }
  } else if (screenState == PASSWORD) {
    for (auto &k: keys) {
      if (hit(x,y,k.x,k.y,k.w,k.h)) {
        String l=k.label;
        if (l=="OK") {
          WiFi.begin(selectedSSID.c_str(), passwordText.c_str());
          screenState=CONNECTING; header("CONNECTING...");
          uint32_t t=millis();
          while (WiFi.status()!=WL_CONNECTED && millis()-t<15000) delay(100);
          if (WiFi.status()==WL_CONNECTED) {
            startCSI(); screenState=CSI; drawCSI();
          } else { screenState=PASSWORD; drawPassword(); }
        } else if (l=="DEL") {
          if (passwordText.length()) passwordText.remove(passwordText.length()-1);
          drawPassword();
        } else if (l=="<") {
          screenState=SCAN; drawScan();
        } else if (l=="SPACE") {
          passwordText += " ";
          drawPassword();
        } else if (l!="SHIFT") {
          passwordText += l;
          drawPassword();
        }
      }
    }
  } else if (screenState == CSI) {
    if (y>=195 && x<160) {
      esp_wifi_set_csi(false);
      screenState=HOME; drawHome();
    } else if (y>=195 && x>=160) {
      esp_wifi_set_csi(false); scanWiFi();
    }
  }
  delay(200);
}
