#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ============================================================================
// CONFIGURACIÓN DE LA RED WI-FI PROPIA (MODO AP)
// ============================================================================
const char* ssid_AP = "Piscicultura_ESP32";
const char* password_AP = "12345678"; // Mínimo 8 caracteres

// Credenciales para el Login HTML
const char* www_user = "admin";
const char* www_pass = "1234";

WebServer server(80);

// Variable para controlar la sesión activa
bool sesionIniciada = false;

// ============================================================================
// CONFIGURACIÓN DE PINES Y UMBRALES
// ============================================================================
const int PIN_DS18B20  = 32;       
const int PIN_PH_ADC   = 34;       
const int PIN_RELAY_O2 = 23;       
const int PIN_RELAY_PH = 22;       

const float TEMP_LIMITE_INFERIOR = 22.0; 
const float PH_LIMITE_INFERIOR   = 6.5;  
const float PH_LIMITE_SUPERIOR   = 8.5;  
const float PH_7_VOLTAGE = 2.50; 

OneWire oneWire(PIN_DS18B20);
DallasTemperature sensors(&oneWire);

// Variables para control de tiempo sin bloqueos (Millis)
unsigned long ultimaLecturaSensores = 0;
const unsigned long INTERVALO_SENSORES = 2000; // Lee sensores cada 2 segundos

float temperaturaActual = 0.0;
float valorPHActual = 7.0;
float voltajePHActual = 2.5;

// Variables para estado visual del proceso de pH en la Web
bool enCicloPH = false;
unsigned long inicioCicloPH = 0;
const unsigned long DURACION_CICLO_PLC = 10000; // 10 segundos visuales alineados con el LOGO!

// ============================================================================
// FUNCIONES AUXILIARES DE SENSORES
// ============================================================================
float leerVoltajePH() {
  long sumaADC = 0;
  const int MUESTRAS = 10;
  for (int i = 0; i < MUESTRAS; i++) {
    sumaADC += analogRead(PIN_PH_ADC);
    delay(2);
  }
  float promedioADC = (float)sumaADC / MUESTRAS;
  return (promedioADC * 3.3) / 4095.0;
}

float calcularPH(float voltaje) {
  return 7.0 + ((PH_7_VOLTAGE - voltaje) / 0.18);
}

// ============================================================================
// VISTA 1: FORMULARIO DE LOGIN HTML PERSONALIZADO
// ============================================================================
void mostrarLoginHTML(String mensajeError = "") {
  String html = "<html lang='es'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Iniciar Sesión - Piscicultura IoT</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background: #eef2f3; text-align: center; margin: 0; padding: 50px 20px; }";
  html += ".login-card { max-width: 380px; margin: auto; background: white; padding: 30px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); }";
  html += "h1 { color: #023e8a; font-size: 22px; margin-bottom: 5px; }";
  html += "h3 { color: #6c757d; font-weight: normal; margin-top: 0; font-size: 13px; margin-bottom: 25px; }";
  html += ".input-group { margin-bottom: 15px; text-align: left; }";
  html += "label { display: block; font-size: 13px; color: #333; margin-bottom: 5px; font-weight: bold; }";
  html += "input[type='text'], input[type='password'] { width: 100%; padding: 10px; border: 1px solid #ccc; border-radius: 6px; box-sizing: border-box; font-size: 14px; }";
  html += "input[type='submit'] { width: 100%; background-color: #0077b6; color: white; border: none; padding: 12px; border-radius: 6px; font-size: 15px; font-weight: bold; cursor: pointer; transition: 0.3s; margin-top: 10px; }";
  html += "input[type='submit']:hover { background-color: #023e8a; }";
  html += ".error { color: #d90429; font-size: 13px; margin-bottom: 15px; font-weight: bold; }";
  html += "</style></head><body>";

  html += "<div class='login-card'>";
  html += "<h1>Monitoreo IoT</h1>";
  html += "<h3>Sistema de Piscicultura</h3>";

  if (mensajeError != "") {
    html += "<div class='error'>" + mensajeError + "</div>";
  }

  html += "<form action='/login' method='POST'>";
  html += "<div class='input-group'><label>Usuario:</label><input type='text' name='usuario' placeholder='Ingrese su usuario' required></div>";
  html += "<div class='input-group'><label>Contraseña:</label><input type='password' name='password' placeholder='Ingrese su contraseña' required></div>";
  html += "<input type='submit' value='Iniciar Sesión'>";
  html += "</form>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

// ============================================================================
// MANEJADORES DE RUTAS WEB
// ============================================================================

void handleLogin() {
  if (server.hasArg("usuario") && server.hasArg("password")) {
    if (server.arg("usuario") == www_user && server.arg("password") == www_pass) {
      sesionIniciada = true;
      server.sendHeader("Location", "/");
      server.send(303); 
      return;
    }
  }
  mostrarLoginHTML("Usuario o contraseña incorrectos");
}

void handleLogout() {
  sesionIniciada = false;
  server.sendHeader("Location", "/");
  server.send(303); 
}

void handleRoot() {
  if (!sesionIniciada) {
    mostrarLoginHTML();
    return;
  }

  bool estadoO2 = (digitalRead(PIN_RELAY_O2) == LOW);

  String html = "<html lang='es'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='2'>"; 
  html += "<title>IoT Piscicultura - AOTCSERVIS</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background: #eef2f3; text-align: center; margin: 0; padding: 20px; }";
  html += ".container { max-width: 600px; margin: auto; background: white; padding: 25px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); position: relative; }";
  html += ".btn-logout { position: absolute; top: 15px; right: 15px; background-color: #e63946; color: white; border: none; padding: 8px 12px; border-radius: 6px; cursor: pointer; text-decoration: none; font-size: 12px; font-weight: bold; transition: 0.3s; }";
  html += ".btn-logout:hover { background-color: #c1121f; }";
  html += "h1 { color: #023e8a; font-size: 22px; margin-bottom: 5px; margin-top: 10px; }";
  html += "h3 { color: #6c757d; font-weight: normal; margin-top: 0; font-size: 14px; }";
  html += ".card { background: #f8f9fa; margin: 15px 0; padding: 15px; border-radius: 8px; border-left: 5px solid #0077b6; text-align: left; }";
  html += ".card.active { border-left-color: #2b9348; }";
  html += ".badge { float: right; padding: 5px 10px; border-radius: 5px; color: white; font-weight: bold; font-size: 12px; }";
  html += ".bg-on { background-color: #2b9348; }";
  html += ".bg-off { background-color: #d90429; }";
  html += ".val { font-size: 20px; font-weight: bold; color: #333; }";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<a href='/logout' class='btn-logout'>Cerrar Sesión</a>";

  html += "<h1>Monitoreo y Control IoT</h1>";
  html += "<h3>Sistema Automatizado de Piscicultura</h3>";

  html += "<div class='card'>";
  html += "<b>Temperatura del Agua:</b><br><span class='val'>" + String(temperaturaActual, 2) + " &deg;C</span>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<b>Nivel de pH (Estimado):</b><br><span class='val'>" + String(valorPHActual, 2) + " pH</span> (" + String(voltajePHActual, 2) + "V)";
  html += "</div>";

  html += "<div class='card " + String(estadoO2 ? "active" : "") + "'>";
  html += "<b>Motor de Oxigenación:</b>";
  if (estadoO2) {
    html += "<span class='badge bg-on'>ENCENDIDO</span>";
  } else {
    html += "<span class='badge bg-off'>APAGADO</span>";
  }
  html += "</div>";

  html += "<div class='card " + String(enCicloPH ? "active" : "") + "'>";
  html += "<b>Sistema de pH (Pulso / Dosificación):</b>";
  if (enCicloPH) {
    html += "<span class='badge bg-on'>DOSIFICANDO (10s)</span>";
  } else {
    html += "<span class='badge bg-off'>ESPERA / REPOSO</span>";
  }
  html += "</div>";

  html += "<p style='font-size:11px; color:#aaa; margin-top:20px;'>Actualización automática cada 2 segundos</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_RELAY_O2, OUTPUT);
  pinMode(PIN_RELAY_PH, OUTPUT);
  digitalWrite(PIN_RELAY_O2, HIGH);
  digitalWrite(PIN_RELAY_PH, HIGH);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  sensors.begin();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid_AP, password_AP);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.println("\n¡Red Wi-Fi creada!");
  Serial.print("Ingresa al Servidor Web: http://");
  Serial.println(IP);

  server.on("/", handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/logout", handleLogout);
  server.begin();
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  // 1. Atender solicitudes web
  server.handleClient();

  // 2. Control del tiempo visual del ciclo de dosificación en la web
  if (enCicloPH && (millis() - inicioCicloPH >= DURACION_CICLO_PLC)) {
    enCicloPH = false; // Finalizan los 10 segundos visuales
  }

  // 3. Lectura periódica de sensores (cada 2 segundos)
  if (millis() - ultimaLecturaSensores > INTERVALO_SENSORES) {
    ultimaLecturaSensores = millis();

    sensors.requestTemperatures();
    float tempLeida = sensors.getTempCByIndex(0);
    if (tempLeida != -127.0) {
      temperaturaActual = tempLeida;
    }

    voltajePHActual = leerVoltajePH();
    valorPHActual = calcularPH(voltajePHActual);

    // --- Control de Oxigenación por Temperatura ---
    if (temperaturaActual < TEMP_LIMITE_INFERIOR) {
      digitalWrite(PIN_RELAY_O2, LOW);  // Enciende motor O2
    } else {
      digitalWrite(PIN_RELAY_O2, HIGH); // Apaga motor O2
    }
  }

  // 4. Control de pH por PULSOS (Sincronizado con el Logo! PLC)
  static unsigned long ultimaCorreccionPH = 0;
  const unsigned long TIEMPO_ESPERA_PH = 45000; // 45 segundos de tregua/reposo para evitar bucles seguidos

  if ((valorPHActual < PH_LIMITE_INFERIOR || valorPHActual > PH_LIMITE_SUPERIOR) && (millis() - ultimaCorreccionPH > TIEMPO_ESPERA_PH)) {
    // Genera el flanco de subida al LOGO! (I2)
    digitalWrite(PIN_RELAY_PH, LOW);  // Cierra el relé
    delay(1000);                      // Pulso de 1 segundo para el PLC
    digitalWrite(PIN_RELAY_PH, HIGH); // Abre el relé de inmediato
    
    // Inicia estado visual de dosificación (10 segundos para la web)
    enCicloPH = true;
    inicioCicloPH = millis();

    // Registra el tiempo de la corrección
    ultimaCorreccionPH = millis();
  }
}