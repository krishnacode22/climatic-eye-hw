
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <BH1750.h>
#include <DHT.h>
#include <TinyGPS++.h>
#include <LoRa.h>

// ============================================================
// CLIMATIC EYE - COMPLETE SENSOR + LORA + ADMIN DEBUG SERVER
// ============================================================
// Same sensor/LoRa wiring as the previous version.
//
// I2C:
//   SDA  -> GPIO 21
//   SCL  -> GPIO 22
//
// GPS:
//   GPS TX -> GPIO 16
//   GPS RX -> GPIO 17
//
// DHT11:
//   DATA -> GPIO 27
//
// Analog:
//   Rain AO -> GPIO 34
//   Soil AO -> GPIO 35
//
// RA-02:
//   SCK   -> GPIO 18
//   MISO  -> GPIO 19
//   MOSI  -> GPIO 23
//   NSS   -> GPIO 5
//   RESET -> GPIO 14
//   DIO0  -> GPIO 26
//
// IMPORTANT:
// Change LORA_FREQUENCY to match the actual RA-02 hardware.
// 433 MHz: 433E6
// 868 MHz: 868E6
// ============================================================


// ===================== PIN DEFINITIONS ======================

#define SDA_PIN 21
#define SCL_PIN 22

#define GPS_RX 16
#define GPS_TX 17

#define DHT_PIN 27
#define DHT_TYPE DHT11

#define RAIN_PIN 34
#define SOIL_PIN 35

#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_RST 14
#define LORA_DIO0 26


// ======================= LORA SETTINGS ======================

#define LORA_FREQUENCY 433E6
#define LORA_SYNC_WORD 0x12


// ==================== CALIBRATION SETTINGS ==================
// These are SAFE STARTING VALUES only.
// Calibrate them using your actual sensors.
//
// For these common analog modules:
//     DRY usually produces a higher ADC value.
//     WET usually produces a lower ADC value.
//
// If your sensor behaves in the opposite direction, swap the
// DRY and WET values.

#define RAIN_DRY_ADC 4095
#define RAIN_WET_ADC 1200

#define SOIL_DRY_ADC 3200
#define SOIL_WET_ADC 1400


// ====================== WIFI SETTINGS ========================
// The ESP32 creates its own Wi-Fi hotspot by default.
// No router or internet connection is required.
//
// Connect phone/laptop to:
//     SSID:     ClimaticEye-Debug
//     Password: climatic123
//
// Then open:
//     http://192.168.4.1
//
// You can optionally enter a router's credentials below.
// The sketch will try STA first and fall back to the AP.

const char* STA_SSID = "";
const char* STA_PASSWORD = "";

const char* AP_SSID = "ClimaticEye-Debug";
const char* AP_PASSWORD = "climatic123";


// ========================= OBJECTS ===========================

Adafruit_BMP280 bmp;
BH1750 lightMeter;
DHT dht(DHT_PIN, DHT_TYPE);

HardwareSerial GPS(2);
TinyGPSPlus gps;

WebServer server(80);


// ========================= STATE =============================

bool bmpOK = false;
bool bh1750OK = false;
bool dhtOK = false;
bool loraOK = false;

bool lastLoraSent = false;

bool dhtReadingOK = false;
bool bmpReadingOK = false;
bool bh1750ReadingOK = false;

unsigned long packetNumber = 0;
unsigned long lastMeasurementMillis = 0;

float temperature = NAN;
float humidity = NAN;

float bmpTemperature = NAN;
float pressure = NAN;
float altitude = NAN;

float lightLux = NAN;

int rainRaw = 0;
int soilRaw = 0;

float rainPercent = NAN;
float soilPercent = NAN;

bool gpsFix = false;
double latitude = 0.0;
double longitude = 0.0;
int satellites = 0;

String lastTelemetryJSON = "{}";
String lastRawLog = "";
String lastError = "";

String wifiMode = "AP";
String wifiIP = "192.168.4.1";


// ===================== WEB PAGE ==============================

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Climatic Eye - Admin Debug</title>
<style>
:root{
 --bg:#0b1220;--panel:#111827;--panel2:#0f172a;
 --text:#e5e7eb;--muted:#94a3b8;--border:#263244;
 --ok:#22c55e;--warn:#f59e0b;--bad:#ef4444;--info:#60a5fa;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);
font-family:Arial,Helvetica,sans-serif}
.wrap{max-width:1250px;margin:0 auto;padding:24px}
header{display:flex;justify-content:space-between;gap:16px;
align-items:flex-start;margin-bottom:20px}
h1{margin:0 0 6px;font-size:28px}
.subtitle{color:var(--muted)}
.status{padding:8px 12px;border-radius:8px;border:1px solid var(--border);
background:var(--panel);font-size:13px;white-space:nowrap}
.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px}
.card{background:var(--panel);border:1px solid var(--border);
border-radius:12px;padding:16px;min-width:0}
.label{color:var(--muted);font-size:12px;text-transform:uppercase;
letter-spacing:.08em}
.value{font-size:26px;font-weight:700;margin-top:7px}
.unit{color:var(--muted);font-size:13px}
.interp{margin-top:8px;font-size:13px}
.section{margin-top:20px}
.section h2{margin:0 0 10px;font-size:18px}
.rows{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
.row{background:var(--panel2);border:1px solid var(--border);
border-radius:10px;padding:12px;display:flex;justify-content:space-between;
gap:16px}
.row span:first-child{color:var(--muted)}
.badge{display:inline-block;padding:4px 8px;border-radius:999px;
font-size:12px;font-weight:700;border:1px solid var(--border)}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}
.info{color:var(--info)}
pre{margin:0;background:#060b14;border:1px solid var(--border);
border-radius:10px;padding:14px;white-space:pre-wrap;word-break:break-word;
overflow:auto;min-height:260px;color:#d1d5db;font-family:Consolas,Monaco,monospace;
font-size:13px;line-height:1.45}
.error{margin-top:10px;color:var(--bad);font-size:13px}
.footer{margin-top:18px;color:var(--muted);font-size:12px}
@media(max-width:950px){.grid{grid-template-columns:repeat(2,1fr)}}
@media(max-width:600px){.wrap{padding:14px}header{flex-direction:column}
.grid,.rows{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="wrap">

<header>
<div>
<h1>Climatic Eye · Admin Debug</h1>
<div class="subtitle">Live local telemetry, interpreted conditions and raw ESP32 diagnostics</div>
</div>
<div id="connection" class="status">Connecting…</div>
</header>

<section class="grid">

<div class="card">
<div class="label">Temperature</div>
<div id="temperature" class="value">--</div>
<div class="unit">°C</div>
<div id="temperatureInterp" class="interp">Waiting…</div>
</div>

<div class="card">
<div class="label">Humidity</div>
<div id="humidity" class="value">--</div>
<div class="unit">%</div>
<div id="humidityInterp" class="interp">Waiting…</div>
</div>

<div class="card">
<div class="label">Pressure</div>
<div id="pressure" class="value">--</div>
<div class="unit">hPa</div>
<div id="pressureInterp" class="interp">Waiting…</div>
</div>

<div class="card">
<div class="label">Sky Condition</div>
<div id="sky" class="value">--</div>
<div class="unit">interpreted from BH1750</div>
<div id="lightInterp" class="interp">Waiting…</div>
</div>

<div class="card">
<div class="label">Rain</div>
<div id="rain" class="value">--</div>
<div class="unit">%</div>
<div id="rainInterp" class="interp">Waiting…</div>
</div>

<div class="card">
<div class="label">Soil Moisture</div>
<div id="soil" class="value">--</div>
<div class="unit">%</div>
<div id="soilInterp" class="interp">Waiting…</div>
</div>

<div class="card">
<div class="label">GPS</div>
<div id="gps" class="value">--</div>
<div class="unit">position fix</div>
<div id="gpsInterp" class="interp">Waiting…</div>
</div>

<div class="card">
<div class="label">LoRa</div>
<div id="lora" class="value">--</div>
<div class="unit">last transmission</div>
<div id="loraInterp" class="interp">Waiting…</div>
</div>

</section>

<section class="section">
<h2>System / Sensor Status</h2>
<div class="rows">
<div class="row"><span>Node ID</span><strong id="nodeId">--</strong></div>
<div class="row"><span>Packet</span><strong id="packet">--</strong></div>
<div class="row"><span>BMP280</span><strong id="bmpStatus">--</strong></div>
<div class="row"><span>BH1750</span><strong id="bhStatus">--</strong></div>
<div class="row"><span>DHT11</span><strong id="dhtStatus">--</strong></div>
<div class="row"><span>GPS</span><strong id="gpsStatus">--</strong></div>
<div class="row"><span>LoRa</span><strong id="loraStatus">--</strong></div>
<div class="row"><span>Web mode</span><strong id="wifiMode">--</strong></div>
<div class="row"><span>ESP32 IP</span><strong id="wifiIP">--</strong></div>
<div class="row"><span>Last update</span><strong id="updated">--</strong></div>
</div>
</section>

<section class="section">
<h2>Raw Sensor Readings</h2>
<div class="rows">
<div class="row"><span>Raindrop ADC</span><strong id="rainRaw">--</strong></div>
<div class="row"><span>Soil ADC</span><strong id="soilRaw">--</strong></div>
<div class="row"><span>BMP Temperature</span><strong id="bmpTempRaw">--</strong></div>
<div class="row"><span>Light</span><strong id="luxRaw">--</strong></div>
<div class="row"><span>GPS Satellites</span><strong id="satellites">--</strong></div>
<div class="row"><span>Latitude</span><strong id="latitude">--</strong></div>
<div class="row"><span>Longitude</span><strong id="longitude">--</strong></div>
</div>
</section>

<section class="section">
<h2>Raw ESP32 Diagnostic Output</h2>
<pre id="rawLog">Waiting for ESP32 data…</pre>
<div id="error" class="error"></div>
</section>

<div class="footer">
Refreshes every 2 seconds. N/A means the firmware deliberately returned a fallback value
because the source reading was unavailable.
</div>

</div>

<script>
const $=id=>document.getElementById(id);

function fmt(v,d=2){
 if(v===null||v===undefined||Number.isNaN(Number(v))) return "N/A";
 return Number(v).toFixed(d);
}

function badge(text,cls){
 return '<span class="badge '+cls+'">'+text+'</span>';
}

function render(d){
 $("temperature").textContent=fmt(d.temperature);
 $("humidity").textContent=fmt(d.humidity);
 $("pressure").textContent=fmt(d.pressure);
 $("sky").textContent=d.sky_condition||"N/A";
 $("rain").textContent=fmt(d.rain_percent);
 $("soil").textContent=fmt(d.soil_percent);

 $("temperatureInterp").textContent=d.temperature_state||"N/A";
 $("humidityInterp").textContent=d.humidity_state||"N/A";
 $("pressureInterp").textContent=d.pressure_state||"N/A";
 $("lightInterp").textContent=(d.light_lux==null?"N/A":fmt(d.light_lux)+" lux");
 $("rainInterp").textContent=d.rain_state||"N/A";
 $("soilInterp").textContent=d.soil_state||"N/A";

 if(d.gps_fix){
   $("gps").textContent="FIX";
   $("gpsInterp").innerHTML=badge((d.satellites||0)+" satellites","ok");
 }else{
   $("gps").textContent="NO FIX";
   $("gpsInterp").innerHTML=badge("Waiting for position","warn");
 }

 $("lora").textContent=d.lora_sent?"SENT":"FAILED";
 $("loraInterp").innerHTML=d.lora_sent?
   badge("Packet transmitted","ok"):badge("Last TX failed","bad");

 $("nodeId").textContent=d.node_id??"N/A";
 $("packet").textContent=d.packet??"N/A";
 $("bmpStatus").textContent=d.bmp_ok?"OK":"FAILED";
 $("bhStatus").textContent=d.bh1750_ok?"OK":"FAILED";
 $("dhtStatus").textContent=d.dht_ok?"OK":"FAILED";
 $("gpsStatus").textContent=d.gps_fix?"FIX":"NO FIX";
 $("loraStatus").textContent=d.lora_ok?(d.lora_sent?"OK / SENT":"OK / TX FAILED"):"FAILED";
 $("wifiMode").textContent=d.wifi_mode??"N/A";
 $("wifiIP").textContent=d.wifi_ip??"N/A";
 $("updated").textContent=new Date().toLocaleTimeString();

 $("rainRaw").textContent=fmt(d.rain_raw,0);
 $("soilRaw").textContent=fmt(d.soil_raw,0);
 $("bmpTempRaw").textContent=d.bmp_temperature==null?"N/A":fmt(d.bmp_temperature)+" °C";
 $("luxRaw").textContent=d.light_lux==null?"N/A":fmt(d.light_lux)+" lux";
 $("satellites").textContent=d.satellites??"N/A";
 $("latitude").textContent=d.gps_fix?fmt(d.latitude,6):"N/A";
 $("longitude").textContent=d.gps_fix?fmt(d.longitude,6):"N/A";

 $("rawLog").textContent=d.raw_log||d.raw_json||"No raw output available.";
 $("error").textContent=d.error||"";

 $("connection").textContent="ESP32 connected";
 $("connection").style.color="var(--ok)";
}

async function update(){
 try{
   const r=await fetch("/api/data",{cache:"no-store"});
   if(!r.ok) throw new Error("HTTP "+r.status);
   render(await r.json());
 }catch(e){
   $("connection").textContent="Disconnected";
   $("connection").style.color="var(--bad)";
   $("error").textContent="Dashboard connection error: "+e.message;
 }
}

update();
setInterval(update,2000);
</script>
</body>
</html>
)rawliteral";


// ======================== HELPERS ============================

String jsonEscape(const String &input)
{
  String out;
  out.reserve(input.length() + 16);

  for (size_t i = 0; i < input.length(); i++)
  {
    char c = input[i];

    switch (c)
    {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:   out += c; break;
    }
  }

  return out;
}

String floatJSON(float value, int decimals)
{
  if (isnan(value))
    return "null";

  return String(value, decimals);
}

String doubleJSON(double value, int decimals)
{
  if (isnan(value))
    return "null";

  return String(value, decimals);
}

int clampInt(int value, int low, int high)
{
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

float adcToPercent(int raw, int dryADC, int wetADC)
{
  if (dryADC == wetADC)
    return NAN;

  float percentage =
      100.0f * ((float)dryADC - (float)raw) /
      ((float)dryADC - (float)wetADC);

  if (percentage < 0) percentage = 0;
  if (percentage > 100) percentage = 100;

  return percentage;
}


// ================= SENSOR INTERPRETATION =====================

String temperatureState(float value)
{
  if (isnan(value))
    return "Unavailable";

  if (value < 15)
    return "Cool";

  if (value < 25)
    return "Comfortable";

  if (value < 32)
    return "Warm";

  return "Hot";
}

String humidityState(float value)
{
  if (isnan(value))
    return "Unavailable";

  if (value < 30)
    return "Dry";

  if (value < 60)
    return "Moderate";

  if (value < 80)
    return "Humid";

  return "Very humid";
}

String pressureState(float value)
{
  if (isnan(value))
    return "Unavailable";

  if (value < 1000)
    return "Low pressure";

  if (value <= 1025)
    return "Normal range";

  return "High pressure";
}

String skyCondition(float lux)
{
  if (isnan(lux))
    return "Unavailable";

  // This is an interpretation from light intensity,
  // not an astronomical weather classification.
  if (lux < 10)
    return "Dark";

  if (lux < 100)
    return "Very dim";

  if (lux < 1000)
    return "Overcast / low light";

  if (lux < 10000)
    return "Bright daylight";

  return "Sunny";
}

String rainState(float percentage)
{
  if (isnan(percentage))
    return "Unavailable";

  if (percentage < 10)
    return "Dry";

  if (percentage < 35)
    return "Slight moisture";

  if (percentage < 70)
    return "Wet";

  return "Rain detected";
}

String soilState(float percentage)
{
  if (isnan(percentage))
    return "Unavailable";

  if (percentage < 20)
    return "Very dry";

  if (percentage < 40)
    return "Dry";

  if (percentage < 70)
    return "Good moisture";

  return "Very wet";
}


// ======================== GPS ================================

void updateGPS()
{
  while (GPS.available())
  {
    gps.encode(GPS.read());
  }

  if (gps.location.isValid())
  {
    gpsFix = true;
    latitude = gps.location.lat();
    longitude = gps.location.lng();
  }
  else
  {
    gpsFix = false;
  }

  if (gps.satellites.isValid())
    satellites = gps.satellites.value();
  else
    satellites = 0;
}


// ====================== SENSOR READ ==========================

void readSensors()
{
  // ---------------- DHT11 ----------------
  if (dhtOK)
  {
    float newHumidity = dht.readHumidity();
    float newTemperature = dht.readTemperature();

    if (!isnan(newHumidity) && !isnan(newTemperature))
    {
      humidity = newHumidity;
      temperature = newTemperature;
      dhtReadingOK = true;
    }
    else
    {
      dhtReadingOK = false;
      humidity = NAN;
      temperature = NAN;
    }
  }
  else
  {
    dhtReadingOK = false;
    humidity = NAN;
    temperature = NAN;
  }

  // ---------------- BMP280 ----------------
  if (bmpOK)
  {
    float t = bmp.readTemperature();
    float p = bmp.readPressure() / 100.0F;

    if (!isnan(t) && !isnan(p) && p > 0)
    {
      bmpTemperature = t;
      pressure = p;
      altitude = bmp.readAltitude(1013.25);
      bmpReadingOK = true;
    }
    else
    {
      bmpReadingOK = false;
      bmpTemperature = NAN;
      pressure = NAN;
      altitude = NAN;
    }
  }
  else
  {
    bmpReadingOK = false;
    bmpTemperature = NAN;
    pressure = NAN;
    altitude = NAN;
  }

  // ---------------- BH1750 ----------------
  if (bh1750OK)
  {
    float lux = lightMeter.readLightLevel();

    if (!isnan(lux) && lux >= 0)
    {
      lightLux = lux;
      bh1750ReadingOK = true;
    }
    else
    {
      bh1750ReadingOK = false;
      lightLux = NAN;
    }
  }
  else
  {
    bh1750ReadingOK = false;
    lightLux = NAN;
  }

  // ---------------- Analog sensors ----------------
  rainRaw = analogRead(RAIN_PIN);
  soilRaw = analogRead(SOIL_PIN);

  rainPercent = adcToPercent(
      rainRaw, RAIN_DRY_ADC, RAIN_WET_ADC);

  soilPercent = adcToPercent(
      soilRaw, SOIL_DRY_ADC, SOIL_WET_ADC);

  // ---------------- GPS ----------------
  updateGPS();
}

// ===================== JSON TELEMETRY =======================

String createTelemetryJSON()
{
  String json;
  json.reserve(900);

  json += "{";

  json += "\"node_id\":\"CE_NODE_01\",";
  json += "\"packet\":" + String(packetNumber) + ",";

  json += "\"temperature\":" + floatJSON(temperature, 2) + ",";
  json += "\"humidity\":" + floatJSON(humidity, 2) + ",";

  json += "\"bmp_temperature\":" +
          floatJSON(bmpTemperature, 2) + ",";

  json += "\"pressure\":" +
          floatJSON(pressure, 2) + ",";

  json += "\"altitude\":" +
          floatJSON(altitude, 2) + ",";

  json += "\"light_lux\":" +
          floatJSON(lightLux, 2) + ",";

  json += "\"rain_raw\":" + String(rainRaw) + ",";
  json += "\"rain_percent\":" + floatJSON(rainPercent, 1) + ",";

  json += "\"soil_raw\":" + String(soilRaw) + ",";
  json += "\"soil_percent\":" + floatJSON(soilPercent, 1) + ",";

  json += "\"gps_fix\":";
  json += gpsFix ? "true," : "false,";

  if (gpsFix)
  {
    json += "\"latitude\":" +
            String(latitude, 6) + ",";

    json += "\"longitude\":" +
            String(longitude, 6) + ",";
  }
  else
  {
    json += "\"latitude\":null,";
    json += "\"longitude\":null,";
  }

  json += "\"satellites\":" + String(satellites) + ",";

  json += "\"bmp_ok\":";
  json += bmpReadingOK ? "true," : "false,";

  json += "\"bh1750_ok\":";
  json += bh1750ReadingOK ? "true," : "false,";

  json += "\"dht_ok\":";
  json += dhtReadingOK ? "true," : "false,";

  json += "\"lora_ok\":";
  json += loraOK ? "true," : "false,";

  json += "\"lora_sent\":";
  json += lastLoraSent ? "true," : "false,";

  json += "\"temperature_state\":\"" +
          jsonEscape(temperatureState(temperature)) + "\",";

  json += "\"humidity_state\":\"" +
          jsonEscape(humidityState(humidity)) + "\",";

  json += "\"pressure_state\":\"" +
          jsonEscape(pressureState(pressure)) + "\",";

  json += "\"sky_condition\":\"" +
          jsonEscape(skyCondition(lightLux)) + "\",";

  json += "\"rain_state\":\"" +
          jsonEscape(rainState(rainPercent)) + "\",";

  json += "\"soil_state\":\"" +
          jsonEscape(soilState(soilPercent)) + "\",";

  json += "\"wifi_mode\":\"" +
          jsonEscape(wifiMode) + "\",";

  json += "\"wifi_ip\":\"" +
          jsonEscape(wifiIP) + "\",";

  json += "\"error\":\"" +
          jsonEscape(lastError) + "\",";

  json += "\"raw_json\":\"" +
          jsonEscape(lastTelemetryJSON) + "\",";

  json += "\"raw_log\":\"" +
          jsonEscape(lastRawLog) + "\"";

  json += "}";

  return json;
}


// ===================== RAW OUTPUT ============================

String createHumanReadableLog()
{
  String s;
  s.reserve(1800);

  s += "========================================\n";
  s += "       CLIMATIC EYE SENSOR DATA\n";
  s += "========================================\n";

  s += "Packet: ";
  s += String(packetNumber);
  s += "\n\n";

  s += "[DHT11]\n";

  s += "Temperature : ";
  if (isnan(temperature))
    s += "ERROR\n";
  else
  {
    s += String(temperature, 2);
    s += " °C\n";
  }

  s += "Humidity    : ";
  if (isnan(humidity))
    s += "ERROR\n";
  else
  {
    s += String(humidity, 2);
    s += " %\n";
  }

  s += "\n[BMP280]\n";

  s += "Temperature : ";
  if (isnan(bmpTemperature))
    s += "ERROR\n";
  else
  {
    s += String(bmpTemperature, 2);
    s += " °C\n";
  }

  s += "Pressure    : ";
  if (isnan(pressure))
    s += "ERROR\n";
  else
  {
    s += String(pressure, 2);
    s += " hPa\n";
  }

  s += "Altitude    : ";
  if (isnan(altitude))
    s += "ERROR\n";
  else
  {
    s += String(altitude, 2);
    s += " m\n";
  }

  s += "\n[BH1750]\n";

  s += "Light       : ";
  if (isnan(lightLux))
    s += "ERROR\n";
  else
  {
    s += String(lightLux, 2);
    s += " lux\n";
  }

  s += "Sky         : ";
  s += skyCondition(lightLux);
  s += "\n";

  s += "\n[RAINDROP]\n";
  s += "Raw ADC     : ";
  s += String(rainRaw);
  s += "\n";

  s += "Rain %      : ";
  if (isnan(rainPercent))
    s += "N/A\n";
  else
  {
    s += String(rainPercent, 1);
    s += " %\n";
  }

  s += "\n[SOIL]\n";
  s += "Raw ADC     : ";
  s += String(soilRaw);
  s += "\n";

  s += "Moisture %  : ";
  if (isnan(soilPercent))
    s += "N/A\n";
  else
  {
    s += String(soilPercent, 1);
    s += " %\n";
  }

  s += "\n[GPS]\n";

  s += "Fix         : ";
  s += gpsFix ? "YES\n" : "NO\n";

  s += "Satellites  : ";
  s += String(satellites);
  s += "\n";

  s += "Latitude    : ";
  if (gpsFix)
    s += String(latitude, 6) + "\n";
  else
    s += "--\n";

  s += "Longitude   : ";
  if (gpsFix)
    s += String(longitude, 6) + "\n";
  else
    s += "--\n";

  s += "\n[LORA]\n";
  s += "Status      : ";
  s += lastLoraSent ? "SENT\n" : "FAILED\n";

  s += "\n[JSON TELEMETRY]\n";
  s += lastTelemetryJSON;
  s += "\n";

  s += "========================================";

  return s;
}


// ======================== LORA ===============================

bool sendLoRa(const String &payload)
{
  if (!loraOK)
    return false;

  LoRa.beginPacket();
  LoRa.print(payload);

  int result = LoRa.endPacket();

  return (result == 1);
}


// ======================== WEB SERVER =========================

void handleRoot()
{
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData()
{
  String json = createTelemetryJSON();

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleNotFound()
{
  server.send(404, "text/plain", "Not found");
}


// ======================= WIFI ================================

void startWiFi()
{
  // Try router only if credentials were supplied.
  if (strlen(STA_SSID) > 0)
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASSWORD);

    Serial.print("Connecting to Wi-Fi");

    unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < 10000)
    {
      delay(250);
      Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
      wifiMode = "STA";
      wifiIP = WiFi.localIP().toString();

      Serial.println("Wi-Fi connected.");
      Serial.print("ESP32 IP: ");
      Serial.println(wifiIP);

      return;
    }

    Serial.println("STA connection failed. Falling back to AP.");
  }

  // Fallback AP: works without a router or internet.
  WiFi.mode(WIFI_AP);

  bool apStarted = WiFi.softAP(
      AP_SSID,
      AP_PASSWORD);

  if (apStarted)
  {
    wifiMode = "AP";
    wifiIP = WiFi.softAPIP().toString();

    Serial.println("Fallback AP started.");
    Serial.print("SSID: ");
    Serial.println(AP_SSID);
    Serial.print("Password: ");
    Serial.println(AP_PASSWORD);
    Serial.print("Open: http://");
    Serial.println(wifiIP);
  }
  else
  {
    wifiMode = "FAILED";
    wifiIP = "N/A";

    Serial.println("ERROR: Wi-Fi AP could not be started.");
  }
}


// ======================== SETUP ==============================

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("      CLIMATIC EYE WEATHER NODE");
  Serial.println("========================================");
  Serial.println();

  lastError = "";

  // ---------------- I2C ----------------
  Wire.begin(SDA_PIN, SCL_PIN);

  // ---------------- BMP280 -------------
  Serial.println("Checking BMP280...");

  if (bmp.begin(0x76))
  {
    bmpOK = true;
    Serial.println("BMP280 found at 0x76");
  }
  else if (bmp.begin(0x77))
  {
    bmpOK = true;
    Serial.println("BMP280 found at 0x77");
  }
  else
  {
    bmpOK = false;
    Serial.println("BMP280 NOT FOUND");
    lastError += "BMP280 unavailable. ";
  }

  // ---------------- BH1750 -------------
  Serial.println("Checking BH1750...");

  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE))
  {
    bh1750OK = true;
    Serial.println("BH1750 found");
  }
  else
  {
    bh1750OK = false;
    Serial.println("BH1750 NOT FOUND");
    lastError += "BH1750 unavailable. ";
  }

  // ---------------- DHT11 --------------
  dht.begin();
  dhtOK = true;
  Serial.println("DHT11 initialized");

  // ---------------- GPS ---------------
  GPS.begin(
      9600,
      SERIAL_8N1,
      GPS_RX,
      GPS_TX);

  Serial.println("NEO-6M GPS initialized");

  // ---------------- ADC ---------------
  pinMode(RAIN_PIN, INPUT);
  pinMode(SOIL_PIN, INPUT);

  // ---------------- LORA --------------
  Serial.println();
  Serial.println("Initializing RA-02 LoRa...");

  SPI.begin(
      LORA_SCK,
      LORA_MISO,
      LORA_MOSI,
      LORA_CS);

  LoRa.setPins(
      LORA_CS,
      LORA_RST,
      LORA_DIO0);

  if (LoRa.begin(LORA_FREQUENCY))
  {
    loraOK = true;

    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.setTxPower(17);
    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);

    Serial.println("RA-02 LoRa initialized");
  }
  else
  {
    loraOK = false;
    Serial.println("RA-02 LoRa initialization FAILED");
    lastError += "LoRa unavailable. ";
  }

  // ---------------- WIFI --------------
  Serial.println();
  startWiFi();

  // ---------------- ROUTES ------------
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/data", HTTP_GET, handleData);
  server.onNotFound(handleNotFound);

  server.begin();

  Serial.println();
  Serial.println("Web server started.");

  if (wifiMode != "FAILED")
  {
    Serial.print("Open: http://");
    Serial.println(wifiIP);
  }

  Serial.println();
  Serial.println("========================================");
  Serial.println("INITIALIZATION STATUS");
  Serial.println("========================================");

  Serial.print("BMP280     : ");
  Serial.println(bmpOK ? "OK" : "FAILED");

  Serial.print("BH1750     : ");
  Serial.println(bh1750OK ? "OK" : "FAILED");

  Serial.print("DHT11      : ");
  Serial.println(dhtOK ? "OK" : "FAILED");

  Serial.println("GPS        : READY");

  Serial.print("LoRa       : ");
  Serial.println(loraOK ? "OK" : "FAILED");

  Serial.print("Web server : ");
  Serial.println(wifiMode == "FAILED" ? "FAILED" : "READY");

  Serial.println();
  Serial.println("Starting telemetry...");
}


// ========================= LOOP ==============================

void loop()
{
  server.handleClient();

  // Feed the GPS continuously so fixes are not lost while web
  // requests are being handled.
  updateGPS();

  if (millis() - lastMeasurementMillis >= 5000 ||
      lastMeasurementMillis == 0)
  {
    lastMeasurementMillis = millis();

    packetNumber++;

    // Read current sensor values.
    readSensors();

    // Create the payload BEFORE transmission.
    // lora_sent is deliberately not placed inside the packet because
    // the result of transmission is unknown until after endPacket().
    lastTelemetryJSON =
      createTelemetryJSON();

    // Send the current packet over LoRa.
    lastLoraSent =
      sendLoRa(lastTelemetryJSON);

    // Update transient error state.
    if (!loraOK)
    {
      lastError = "LoRa module unavailable.";
    }
    else if (!lastLoraSent)
    {
      lastError = "LoRa transmission failed on last packet.";
    }
    else
    {
      // Preserve non-LoRa sensor errors if any.
      if (lastError == "LoRa transmission failed on last packet." ||
          lastError == "LoRa module unavailable.")
      {
        lastError = "";
      }
    }

    // Generate the human-readable raw output AFTER the TX result
    // is known, so it matches the serial monitor.
    lastRawLog = createHumanReadableLog();

    Serial.println();
    Serial.println(lastRawLog);
  }

  delay(2);
}