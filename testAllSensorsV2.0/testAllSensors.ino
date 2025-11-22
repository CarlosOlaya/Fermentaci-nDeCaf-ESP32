#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1331.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>
#include "SparkFun_SCD4x_Arduino_Library.h" // SCD41

// -------- CONFIGURACIÓN GENERAL --------
#define N8N_URL "https://n8n-carlos-proyecto.eastus2.cloudapp.azure.com/webhook/write"
#define MAX_WIFI_INTENTOS 5
bool wifiConectado = false;

// -------- PINES --------
#define PIN_TDS 35
#define PIN_ONEWIRE_DS18B 14  
#define PIN_BOMBA 27
#define PIN_BOTON 26
#define PIN_PH 34
#define CS 5
#define RST 16
#define DC 17
#define SCLK 18
#define MOSI 23

#define RELAY_ON LOW
#define RELAY_OFF HIGH

// -------- OBJETOS --------
Adafruit_SSD1331 pantalla = Adafruit_SSD1331(&SPI, CS, DC, RST);
OneWire oneWire(PIN_ONEWIRE_DS18B);
DallasTemperature sensorDS18B20(&oneWire);
SCD4x sensorSCD41;

// -------- DATOS --------
typedef struct {
  float temp_c_ds;
  bool temp_ok;

  float tds_voltaje, tds_ppm;
  bool tds_ok;

  float ph;
  bool ph_ok;

  uint16_t co2;     // <-- nuevo
  float temp_scd;   // <-- nuevo
  float hum_scd;    // <-- nuevo
  bool scd_ok;      // <-- nuevo

  bool bomba_activa;
  bool muestreo_en_progreso;

  unsigned long tiempo_ms;
  int contador_restante_ph;
  unsigned long tiempo_proceso;
} DatosSensores;

DatosSensores datosCompartidos;
SemaphoreHandle_t mutexDatos;

// -------- COLA Y TAREAS --------
QueueHandle_t colaEventos;
TaskHandle_t tSensores, tPantalla, tMuestreo, tAuto, tBoton, tEnvio;
enum TipoEvento { EVENTO_MANUAL, EVENTO_AUTOMATICO };

// -------- COLORES --------
#define COLOR_DORADO 0xFD20
#define COLOR_TEMP 0xFA60
#define COLOR_TDS 0xB004
#define COLOR_PH 0xFFE4
#define COLOR_TITULO 0xFFFF

// -------- CONFIGURACIÓN --------
#define CALIBRATION_FACTOR 1.00
#define INTERVALO_ENVIO_NORMAL 30000 // 30 segundos
#define INTERVALO_PH 120             // 2 minutos

// -------- FUNCIONES --------
float leerVoltajeTDS(int pin) {
  int crudo = analogRead(pin);
  return (crudo / 4095.0f) * 3.3f;
}

float convertirTDSdesdeVoltaje(float voltaje) {
  float tds = (133.42 * pow(voltaje, 3) - 255.86 * pow(voltaje, 2) + 857.39 * voltaje) * 0.5;
  return tds * CALIBRATION_FACTOR;
}

// -------- CALIBRACIÓN pH --------
#define PH_NEUTRAL_VOLTAGE 1.65
#define PH_VOLTAGE_PER_UNIT 0.18

float leerPH(int pin) {
  float v = (analogRead(pin) / 4095.0f) * 3.3f;
  float ph = 7.0 - ((v - PH_NEUTRAL_VOLTAGE) / PH_VOLTAGE_PER_UNIT);
  if (ph < 0) ph = 0;
  if (ph > 14) ph = 14;
  return ph;
}

// -------- CONEXIÓN WIFI MULTIRED --------
void conectarWiFi() {
  struct { const char* ssid; const char* pass; } redes[] = {
    {"HOGAR OLAYA", "solrac2025"},
    {"DIVERSO", "Diverso#2024"},
    {"yosoycecar", "yosoycecar"}
  };

  wifiConectado = false;
  const int numRedes = sizeof(redes) / sizeof(redes[0]);

  for (int i = 0; i < numRedes; i++) {
    WiFi.begin(redes[i].ssid, redes[i].pass);
    int intentos = 0;

    while (intentos < MAX_WIFI_INTENTOS && WiFi.status() != WL_CONNECTED) {
      delay(3000);
      intentos++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiConectado = true;
      break;
    } else {
      WiFi.disconnect(true);
      delay(2000);
    }
  }
}

// -------- NTP --------
void sincronizarHoraNTP() {
  if (!wifiConectado) return;
  configTime(-5 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  getLocalTime(&timeinfo);
}

// -------- ENVÍO A N8N --------
void enviarDatosAN8N(DatosSensores d) {
  if (!wifiConectado || WiFi.status() != WL_CONNECTED) return;

  struct tm timeinfo;
  char bufferHora[25];
  if (getLocalTime(&timeinfo))
    strftime(bufferHora, sizeof(bufferHora), "%Y-%m-%dT%H:%M:%S%z", &timeinfo);
  else
    sprintf(bufferHora, "sin_hora");

  HTTPClient http;
  http.begin(N8N_URL);
  http.addHeader("Content-Type", "application/json");

  // JSON INCLUYENDO SCD41
  String payload = "{";
  payload += "\"temperatura\":" + String(d.temp_c_ds, 2) + ",";
  payload += "\"tds\":" + String(d.tds_ppm, 2) + ",";
  payload += "\"ph\":" + String(d.ph, 2) + ",";
  payload += "\"co2\":" + String(d.co2) + ",";
  payload += "\"temp_scd\":" + String(d.temp_scd, 2) + ",";
  payload += "\"hum_scd\":" + String(d.hum_scd, 2) + ",";
  payload += "\"tiempo_proceso\":" + String(d.tiempo_proceso) + ",";
  payload += "\"wifi_conectado\":" + String(wifiConectado ? "true" : "false") + ",";
  payload += "\"timestamp\":\"" + String(bufferHora) + "\"";
  payload += "}";

  http.POST(payload);
  http.end();
}

// -------- TAREA SENSORES --------
void TareaSensores(void *pvParameters) {
  const TickType_t periodo = pdMS_TO_TICKS(5000);

  for (;;) {
    DatosSensores local;

    if (xSemaphoreTake(mutexDatos, 50)) {
      local = datosCompartidos;
      xSemaphoreGive(mutexDatos);
    }

    // DS18B20
    sensorDS18B20.requestTemperatures();
    float temp = sensorDS18B20.getTempCByIndex(0);
    if (temp != DEVICE_DISCONNECTED_C) {
      local.temp_c_ds = temp;
      local.temp_ok = true;
    }

    // TDS
    float voltaje = leerVoltajeTDS(PIN_TDS);
    local.tds_voltaje = voltaje;
    local.tds_ppm = convertirTDSdesdeVoltaje(voltaje);
    local.tds_ok = true;

    // SCD41
    if (sensorSCD41.readMeasurement()) {
      local.co2 = sensorSCD41.getCO2();
      local.temp_scd = sensorSCD41.getTemperature();
      local.hum_scd = sensorSCD41.getHumidity();
      local.scd_ok = true;
    }

    local.tiempo_ms = millis();

    if (xSemaphoreTake(mutexDatos, 200)) {
      datosCompartidos = local;
      xSemaphoreGive(mutexDatos);
    }

    vTaskDelay(periodo);
  }
}

// -------- TAREA ENVÍO --------
void TareaEnvioPeriodico(void *pvParameters) {
  const TickType_t periodo = pdMS_TO_TICKS(INTERVALO_ENVIO_NORMAL);
  vTaskDelay(10000);

  for (;;) {
    DatosSensores copia;

    if (xSemaphoreTake(mutexDatos, 100)) {
      copia = datosCompartidos;
      xSemaphoreGive(mutexDatos);
    }

    enviarDatosAN8N(copia);
    vTaskDelay(periodo);
  }
}

// -------- PANTALLA --------
void mostrarBonito(DatosSensores d) {
  pantalla.fillScreen(0x0000);
  pantalla.setTextSize(1);

  pantalla.setTextColor(COLOR_DORADO);
  pantalla.setCursor(0, 0);
  pantalla.print("Fermentacion C.");

  pantalla.setTextColor(COLOR_TDS);
  pantalla.setCursor(0, 14);
  pantalla.printf("TDS: %.0f", d.tds_ppm);

  pantalla.setTextColor(COLOR_TEMP);
  pantalla.setCursor(0, 23);
  pantalla.printf("TS: %.1f", d.temp_c_ds);

  pantalla.setTextColor(COLOR_PH);
  pantalla.setCursor(0, 32);
  pantalla.printf("pH: %.2f", d.ph);

  pantalla.setTextColor(COLOR_TITULO);
  pantalla.setCursor(0, 41);
  pantalla.printf("B:%s", d.bomba_activa ? "ON" : "OFF");

  pantalla.setCursor(0, 50);
  pantalla.setTextColor(0x07FF);
  pantalla.printf("t:%ds", d.contador_restante_ph);

  // ---- COLUMNA DERECHA SCD41 ----
  pantalla.setTextColor(0xFBE0);
  pantalla.setCursor(56, 14);
  pantalla.printf("C:%d", d.co2);

  pantalla.setTextColor(0xFBA0);
  pantalla.setCursor(56, 23);
  pantalla.printf("TA:%.1f", d.temp_scd);

  pantalla.setTextColor(0x07FF);
  pantalla.setCursor(56, 32);
  pantalla.printf("H:%.0f%%", d.hum_scd);

  // WIFI
  pantalla.setCursor(70, 50);
  pantalla.setTextColor(wifiConectado ? 0x07E0 : 0xF800);
  pantalla.print(wifiConectado ? "WIFI" : "NOWI");
}

void TareaPantalla(void *pvParameters) {
  for (;;) {
    DatosSensores c;
    if (xSemaphoreTake(mutexDatos, 100)) {
      c = datosCompartidos;
      xSemaphoreGive(mutexDatos);
    }
    mostrarBonito(c);
    vTaskDelay(1000);
  }
}

// -------- MUESTREO PH --------
void TareaMuestreoPH(void *pvParameters) {
  TipoEvento ev;

  for (;;) {
    if (xQueueReceive(colaEventos, &ev, portMAX_DELAY)) {
      if (xSemaphoreTake(mutexDatos, 100)) {
        datosCompartidos.muestreo_en_progreso = true;
        datosCompartidos.bomba_activa = true;
        xSemaphoreGive(mutexDatos);
      }

      digitalWrite(PIN_BOMBA, RELAY_ON);
      vTaskDelay(10000);
      digitalWrite(PIN_BOMBA, RELAY_OFF);

      float ph = leerPH(PIN_PH);
      if (xSemaphoreTake(mutexDatos, 200)) {
        datosCompartidos.ph = ph;
        datosCompartidos.ph_ok = true;
        datosCompartidos.bomba_activa = false;
        datosCompartidos.muestreo_en_progreso = false;
        xSemaphoreGive(mutexDatos);
      }

      DatosSensores copia;
      if (xSemaphoreTake(mutexDatos, 100)) {
        copia = datosCompartidos;
        xSemaphoreGive(mutexDatos);
      }

      enviarDatosAN8N(copia);
    }
  }
}

// -------- CONTADOR --------
void TareaTiempoProceso(void *pvParameters) {
  for (;;) {
    if (xSemaphoreTake(mutexDatos, 50)) {
      datosCompartidos.tiempo_proceso++;
      xSemaphoreGive(mutexDatos);
    }
    vTaskDelay(1000);
  }
}

// -------- AUTOMÁTICO PH --------
void TareaBombaAuto(void *pvParameters) {
  const TickType_t periodo = pdMS_TO_TICKS(1000);
  int contador = INTERVALO_PH;

  for (;;) {
    if (--contador <= 0) {
      contador = INTERVALO_PH;
      TipoEvento e = EVENTO_AUTOMATICO;
      xQueueSend(colaEventos, &e, 0);
    }

    if (xSemaphoreTake(mutexDatos, 100)) {
      datosCompartidos.contador_restante_ph = contador;
      xSemaphoreGive(mutexDatos);
    }

    vTaskDelay(periodo);
  }
}

// -------- BOTÓN --------
void TareaBotonManual(void *pvParameters) {
  bool prev = HIGH;
  const TickType_t debounce = pdMS_TO_TICKS(50);

  for (;;) {
    bool estado = digitalRead(PIN_BOTON);
    if (estado == LOW && prev == HIGH) {
      vTaskDelay(debounce);
      if (digitalRead(PIN_BOTON) == LOW) {
        TipoEvento e = EVENTO_MANUAL;
        xQueueSend(colaEventos, &e, 0);

        while (digitalRead(PIN_BOTON) == LOW) vTaskDelay(10);
      }
    }
    prev = estado;
    vTaskDelay(20);
  }
}

// -------- SETUP --------
void setup() {
  Serial.begin(115200);
  delay(500);

  conectarWiFi();
  sincronizarHoraNTP();

  mutexDatos = xSemaphoreCreateMutex();
  colaEventos = xQueueCreate(5, sizeof(TipoEvento));
  memset(&datosCompartidos, 0, sizeof(datosCompartidos));
  datosCompartidos.contador_restante_ph = INTERVALO_PH;

  pinMode(PIN_BOMBA, OUTPUT);
  digitalWrite(PIN_BOMBA, RELAY_OFF);
  pinMode(PIN_BOTON, INPUT_PULLUP);

  SPI.begin();
  pantalla.begin();
  pantalla.fillScreen(0x0000);
  pantalla.setTextColor(0xFFFF);
  pantalla.setCursor(0, 0);
  pantalla.println("Iniciando...");

  sensorDS18B20.begin();

  // ---- SCD41 ----
  Wire.begin(21, 22);
  if (sensorSCD41.begin()) {
    sensorSCD41.startPeriodicMeasurement();
  }

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_TDS, ADC_11db);
  analogSetPinAttenuation(PIN_PH, ADC_11db);

  xTaskCreatePinnedToCore(TareaSensores, "Sensores", 4096, NULL, 5, &tSensores, 1);
  xTaskCreatePinnedToCore(TareaPantalla, "Pantalla", 4096, NULL, 3, &tPantalla, 1);
  xTaskCreatePinnedToCore(TareaTiempoProceso, "TiempoProceso", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TareaMuestreoPH, "MuestreoPH", 4096, NULL, 4, &tMuestreo, 0);
  xTaskCreatePinnedToCore(TareaBombaAuto, "Auto", 4096, NULL, 2, &tAuto, 0);
  xTaskCreatePinnedToCore(TareaBotonManual, "Boton", 2048, NULL, 6, &tBoton, 0);
  xTaskCreatePinnedToCore(TareaEnvioPeriodico, "EnvioPeriodico", 4096, NULL, 2, &tEnvio, 0);
}

// -------- LOOP --------
void loop() {
  vTaskDelay(1000);
}
