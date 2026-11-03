/* =============================================================================
 *  PROJET RED/BLUE - "Danfoss Supermarket Attack"  |  GI-CNAM 4 / cybersécurité OT
 *  Fichier  : esp32_bme280_sensor.ino   (v4 : ESP8266 + BMP280 + OLED + LED RGB)
 *  Rôle     : SONDE FROID (simule le régulateur Danfoss AK-SM d'un rayon frais)
 *
 *  >>> MATÉRIEL RÉEL (ta station météo en laiton) :
 *      - Carte  : ESP8266 (NodeMCU / Wemos D1)  ← PAS un ESP32
 *      - Capteur: BMP280 (température + pression, PAS d'humidité)
 *      - Écran  : OLED SSD1306 128x64
 *      - LED RGB: broches D5/D6/D7 (indicateur froid visible de loin)
 *      - Bus I2C: SDA = D2, SCL = D3   (c'est LE point qui bloquait l'affichage)
 *
 *  Sélection carte dans l'IDE : Outils → Type de carte → ESP8266 → NodeMCU 1.0
 *  (ou la carte exacte de ta station, celle qui compilait déjà ton ancien code).
 *
 *  >>> VERSION "VULNÉRABLE" (Phase 1).  Durcissements BLEU : // [DURCISSEMENT BLEU]
 * ============================================================================= */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>   // serveur web embarque
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

/* --------------------------- BROCHES (comme ta station) -------------------- */
#define SDA_PIN D2
#define SCL_PIN D3
#define RED_PIN   D5
#define GREEN_PIN D6
#define BLUE_PIN  D7

/* ----------------------------- CONFIG RÉSEAU -------------------------------- */
const char* WIFI_SSID = "Kiiway";
const char* WIFI_PASS = "endgame010824";
const char* MQTT_HOST = "10.227.83.209";   // PC Node-RED / Mosquitto (VICTIME)
uint16_t    MQTT_PORT = 1883;              // [DURCISSEMENT BLEU] -> 8883 (TLS)
const char* MQTT_USER = "";                // [DURCISSEMENT BLEU] -> compte "sensor"
const char* MQTT_PASS = "";
//const char* CLIENT_ID = "sensor-" GROUP;   // identifiant unique sur le broker partagé
float setpoint    = 4.0;
float tempReal    = 0.0;    // vraie température BMP280
float pressReal   = 0.0;    // vraie pression hPa
float defrostBias = 0.0;
bool  defrost     = false;
bool  alarmMuted  = false;
float tempPub     = 0.0;
bool  alarmActive = false;

unsigned long lastPub = 0;
const unsigned long PUB_PERIOD = 2000;

/* --------------------------- GROUPE & TOPICS MQTT -------------------------- */
// >>> IDENTIFIANT DU GROUPE : g1, g2, g3 ou g4 (un par poste victime).
#define GROUP "g1"
#define T_TELEMETRY ("market/" GROUP "/fridge/telemetry")
#define T_CMD       ("market/" GROUP "/fridge/cmd")
#define T_STATUS    ("market/" GROUP "/fridge/status")
#define T_NOTIFY    ("market/" GROUP "/tab5/notify")

const char* CLIENT_ID = "sensor-" GROUP;   // identifiant unique sur le broker partagé

/* --------------------------- ÉCRAN OLED SSD1306 ---------------------------- */
#define OLED_W 128
#define OLED_H 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);
bool oledPresent = false;

/* ----------------------- COMPORTEMENT DÉGIVRAGE ---------------------------- */
#define SIMULATE_DEFROST_EFFECT true   // l'attaque "defrost" fait monter la temp publiée
const float DEFROST_RISE = 14.0;       // °C au-dessus du réel au max du dégivrage
const float ALARM_LIMIT  = 8.0;        // chaîne du froid rompue au-dessus de ce seuil

/* --------------------------- OBJETS / ÉTAT -------------------------------- */
WiFiClient   net;
PubSubClient mqtt(net);
Adafruit_BMP280 bmp;
bool  sensorOk = false;

/* ------------------------- SERVEUR WEB EMBARQUE --------------------------- */
// Variante ESP32 : #include <WebServer.h>  +  WebServer server(80);
ESP8266WebServer server(80);
const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Station Fridge1</title>
<style>
 body{margin:0;background:#070a12;color:#eaf2ff;font-family:system-ui,Segoe UI,Roboto,sans-serif;
   display:grid;place-items:center;min-height:100vh}
 .card{background:#0f1726;border:1px solid #223;border-radius:16px;padding:22px 26px;width:min(92vw,420px);
   box-shadow:0 10px 40px rgba(0,0,0,.4)}
 h1{font-size:1.05rem;margin:0 0 .6rem;color:#9fb3d0;font-weight:600}
 .temp{font-size:3.6rem;font-weight:800;color:#38bdf8;line-height:1.1}
 .row{display:flex;justify-content:space-between;padding:.4rem 0;border-top:1px solid #1b2740}
 .k{color:#9fb3d0}.v{font-weight:700}
 .alarm{margin-top:.6rem;color:#fff;background:#b11020;border-radius:8px;padding:.4rem .6rem;
   text-align:center;font-weight:700;display:none}
 .dot{display:inline-block;width:.6rem;height:.6rem;border-radius:50%;background:#22c55e;margin-left:.3rem}
 .off{background:#ef4444}
</style></head><body>
<div class="card">
 <h1>RAYON FRAIS &mdash; Fridge1 <span id="lnk" class="dot"></span></h1>
 <div id="temp" class="temp">-- &deg;C</div>
 <div class="row"><span class="k">Pression</span><span class="v"><span id="press">--</span> hPa</span></div>
 <div class="row"><span class="k">D&eacute;givrage</span><span class="v" id="def">--</span></div>
 <div class="row"><span class="k">MQTT</span><span class="v" id="mqtt">--</span></div>
 <div id="alarm" class="alarm">CHA&Icirc;NE DU FROID ROMPUE</div>
</div>
<script>
async function u(){
 try{
  const d=await (await fetch('/data')).json();
  temp.innerHTML=(d.sensor_ok? d.temp.toFixed(1):'--')+' &deg;C';
  press.textContent=d.pressure.toFixed(0);
  def.textContent=d.defrost?'ACTIF':'off';
  mqtt.textContent=d.mqtt?'connecte':'deconnecte';
  lnk.className='dot'+(d.mqtt?'':' off');
  temp.style.color=d.alarm?'#ff4d4d':'#38bdf8';
  alarm.style.display=d.alarm?'block':'none';
 }catch(e){ mqtt.textContent='hors ligne'; lnk.className='dot off'; }
}
setInterval(u,2000); u();
</script></body></html>
)HTML";

void handleRoot(){ server.send_P(200, "text/html", PAGE); }
void handleData(){
  String j = "{";
  j += "\"temp\":"      + String(tempPub,1);
  j += ",\"pressure\":" + String(pressReal,1);
  j += ",\"defrost\":"  + String(defrost ? "true":"false");
  j += ",\"alarm\":"    + String(alarmActive ? "true":"false");
  j += ",\"sensor_ok\":"+ String(sensorOk ? "true":"false");
  j += ",\"mqtt\":"     + String(mqtt.connected() ? "true":"false");
  j += "}";
  server.send(200, "application/json", j);
}



/* ------------------------------- LED RGB --------------------------------- */
// Reprend exactement ta logique (LED common-anode : valeur inversée).
void setLEDColor(int r, int g, int b) {
  analogWrite(RED_PIN,   255 - r);
  analogWrite(GREEN_PIN, 255 - g);
  analogWrite(BLUE_PIN,  255 - b);
}

/* ============================ SCANNER I2C (debug câblage) ================= */
void scanI2C() {
  Serial.println("[I2C] Scan du bus (SDA=D2, SCL=D3)...");
  byte count = 0;
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("   trouvé: 0x%02X", a);
      if (a == 0x3C || a == 0x3D) Serial.print("  (OLED SSD1306 ?)");
      if (a == 0x76 || a == 0x77) Serial.print("  (BMP280 ?)");
      Serial.println();
      count++;
    }
  }
  if (!count) Serial.println("   AUCUN périphérique I2C ! -> VCC/GND/SDA(D2)/SCL(D3).");
}

/* ============================ WIFI / MQTT ================================= */
void wifiConnect() {
  Serial.printf("[WiFi] Connexion à %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    setLEDColor(0, 0, 255); delay(200); setLEDColor(0, 0, 0); delay(200);  // clignote bleu
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\n[WiFi] OK  IP=%s  MAC=%s\n",
                  WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str());
  else Serial.println("\n[WiFi] ÉCHEC (SSID/mdp ? 2.4GHz ?)");
}

void onMessage(char* topic, byte* payload, unsigned int len) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload, len)) return;
  // [DURCISSEMENT BLEU] : aucun contrôle d'origine -> injection de commande possible.
  if (doc.containsKey("defrost"))  defrost    = doc["defrost"].as<bool>();
  if (doc.containsKey("setpoint")) setpoint   = doc["setpoint"].as<float>();
  if (doc.containsKey("mute"))     alarmMuted = doc["mute"].as<bool>();
  Serial.printf("[CMD] defrost=%d setpoint=%.1f mute=%d\n", defrost, setpoint, alarmMuted);
}

void mqttConnect() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  while (!mqtt.connected()) {
    Serial.print("[MQTT] Connexion...");
    bool ok = (strlen(MQTT_USER) > 0)
                ? mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS)
                : mqtt.connect(CLIENT_ID);
    if (ok) {
      Serial.println(" OK");
      mqtt.subscribe(T_CMD, 1);        // QoS 1 : la commande critique (dégivrage) doit arriver même sous flood
      mqtt.publish(T_STATUS, "{\"state\":\"online\",\"model\":\"AK-SM800\"}", true);
    } else { Serial.printf(" échec rc=%d, retry 2s\n", mqtt.state()); delay(2000); }
  }
}

/* ============================== MESURE + MODÈLE =========================== */
void readAndPublish() {
  if (sensorOk) {
    tempReal  = bmp.readTemperature();        // °C réels (= temp du frigo une fois dedans)
    pressReal = bmp.readPressure() / 100.0F;  // Pa -> hPa
  } else { tempReal = NAN; pressReal = NAN; }

  // Effet "dégivrage" ajouté par-dessus la mesure réelle
  if (SIMULATE_DEFROST_EFFECT) {
    float target = defrost ? DEFROST_RISE : 0.0;
    defrostBias += (target - defrostBias) * (defrost ? 0.06 : 0.10);
  } else defrostBias = 0.0;

  tempPub = (isnan(tempReal) ? 0.0 : tempReal) + defrostBias;
  alarmActive = (tempPub > ALARM_LIMIT) && !alarmMuted;

  // LED : rouge si chaîne du froid rompue, vert sinon (visible de loin pendant l'attaque)
  if (!sensorOk)        setLEDColor(255, 60, 0);   // orange = capteur KO
  else if (alarmActive) setLEDColor(255, 0, 0);    // ROUGE = alarme / attaque réussie
  else                  setLEDColor(0, 255, 0);    // vert = froid OK

  // Publication MQTT
  StaticJsonDocument<256> doc;
  doc["temp"]      = round(tempPub * 10) / 10.0;                          // réel + effet dégivrage
  doc["temp_raw"]  = isnan(tempReal)  ? (float)0 : round(tempReal*10)/10.0;   // vraie mesure
  doc["pressure"]  = isnan(pressReal) ? (float)0 : round(pressReal*10)/10.0;  // hPa réels
  doc["setpoint"]  = setpoint;
  doc["defrost"]   = defrost;
  doc["alarm"]     = alarmActive;
  doc["sensor"]    = "BMP280";
  doc["sensor_ok"] = sensorOk;
  doc["ts"]        = millis();
  char buf[256]; size_t n = serializeJson(doc, buf);
  mqtt.publish(T_TELEMETRY, buf, n);          // [DURCISSEMENT BLEU] : en clair (MITM)

  if (alarmActive) mqtt.publish(T_NOTIFY,
      "{\"level\":\"critical\",\"msg\":\"CHAINE DU FROID ROMPUE - Fridge1\"}");

  Serial.printf("[MESURE] reel=%.1fC publie=%.1fC press=%.1fhPa defrost=%d alarme=%d\n",
                tempReal, tempPub, pressReal, defrost, alarmActive);
}

/* ============================== AFFICHAGE OLED =========================== */
void drawOLED() {
  if (!oledPresent) return;
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.println("RAYON FRAIS  Fridge1");

  if (!sensorOk) {
    display.setCursor(0, 20); display.println("BMP280 ABSENT !");
    display.setCursor(0, 32); display.println("Voir scanner I2C");
    display.setCursor(0, 44); display.println("(moniteur serie)");
    display.display();
    return;
  }

  display.setTextSize(2);
  display.setCursor(0, 14);
  display.print(tempPub, 1); display.print((char)247); display.print("C");

  display.setTextSize(1);
  display.setCursor(0, 38);
  display.print("Pres: "); display.print(pressReal, 0); display.println(" hPa");

  display.setCursor(0, 50);
  display.print(defrost ? "DEGIV:ON " : "DEGIV:off ");
  display.print(mqtt.connected() ? "MQTT:OK" : "MQTT:XX");

  if (alarmActive) {
    display.fillRect(84, 12, 44, 18, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK); display.setTextSize(1);
    display.setCursor(90, 14); display.println("AL-");
    display.setCursor(90, 22); display.println("ARM");
    display.setTextColor(SSD1306_WHITE);
  }
  display.display();
}

/* ================================= SETUP ================================= */
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== Sonde Fridge1 (v4) - ESP8266 + BMP280 + OLED ==");

  pinMode(RED_PIN, OUTPUT); pinMode(GREEN_PIN, OUTPUT); pinMode(BLUE_PIN, OUTPUT);
  setLEDColor(0, 0, 0);

  Wire.begin(SDA_PIN, SCL_PIN);              // <<< LE correctif : I2C sur D2/D3
  scanI2C();

  oledPresent = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  Serial.printf("[OLED] %s (0x%02X)\n", oledPresent ? "détecté" : "ABSENT", OLED_ADDR);
  if (oledPresent) {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); display.println("Init sonde froid...");
    display.display();
  }

  sensorOk = bmp.begin(0x76) || bmp.begin(0x77);
  Serial.printf("[BMP280] %s\n", sensorOk ? "détecté" : "ABSENT (cablage/adresse I2C)");

  wifiConnect();
  mqttConnect();

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.printf("[WEB] page embarquee : http://%s/\n", WiFi.localIP().toString().c_str());
}

/* ================================= LOOP ================================== */
void loop() {
  if (WiFi.status() != WL_CONNECTED) wifiConnect();
  if (!mqtt.connected())             mqttConnect();
  mqtt.loop();
  server.handleClient();
  if (millis() - lastPub > PUB_PERIOD) {
    lastPub = millis();
    readAndPublish();
    drawOLED();
  }
}

/* =============================================================================
 *  RÉCAP CÂBLAGE (ta station) :  SDA=D2  SCL=D3  |  BMP280 0x76  |  OLED 0x3C
 *                                LED: R=D5 G=D6 B=D7
 *  Si "ABSENT" : le scanner I2C au boot liste ce qui répond sur le bus.
 *
 *  MÉMO BLEU : D6 MQTT/TLS (8883, BearSSL) + vérif certificat ; D1/D9 compte "sensor" + ACL.
 *  NOTE : "temp" = réel + effet dégivrage ; "temp_raw" = vraie mesure (vérité terrain).
 *         BMP280 => pas d'humidité ; on publie la PRESSION. (BME280 requis pour l'humidité.)
 * ============================================================================= */

/* =============================================================================
 *  PARADE QoS (attaque DDoS) :
 *   - telemetry = flux de masse -> QoS 0 (perte toleree ; PubSubClient ne publie qu'en QoS 0).
 *   - cmd degivrage / alarme = critiques -> abonnement en QoS 1 (voir mqtt.subscribe(T_CMD,1)) ;
 *     cote Node-RED, republier la commande en QoS 2.
 *   - La QoS garantit l'ARRIVEE du message critique ; les limites broker
 *     (max_inflight / max_queued / message_size_limit) garantissent la DISPONIBILITE.
 *   - Pour publier un critique en QoS 1/2 DEPUIS l'ESP : lib 256dpi/arduino-mqtt ou AsyncMqttClient.
 * ============================================================================= */
