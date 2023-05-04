#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include "ESP32_MailClient.h"

#define DEVICE "ESP32"

// Import the InfluxDB libraries
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
//Importam libaria DHT pentru senzor si setam cei 2 parametri(Pin-ul si tipuls enzorului)
#include "DHT.h"
#define DHTPIN 27
#define DHTTYPE DHT11
// Configurare senzorului DHT11
DHT dht(DHTPIN, DHTTYPE);

// Datele pentru reteaua de wifi
const char* ssid = "No Network";
const char* password = "a7eeb92d";

// Definim detaliile pentru InfluxDB

// InfluxDB v2 server url (Use: InfluxDB UI -> Load Data -> Client Libraries)
#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"

// InfluxDB v2 server or cloud API authentication token (Use: InfluxDB UI -> Data -> Tokens -> <select token>)
#define INFLUXDB_TOKEN "xfsspuK4bx6AknmLuPBgpRJIPBM8tHAXF-T_QZA6EP1Uf6IgQUN44hgnQ9erO1_gDdEqOdhsC_7m0a6rmxlvGA=="

// InfluxDB v2 organization id (Use: InfluxDB UI -> User -> About -> Common Ids )
#define INFLUXDB_ORG "poparobert999@gmail.com"

// InfluxDB v2 bucket name (Use: InfluxDB UI -> Data -> Buckets)
#define INFLUXDB_BUCKET "ESP32"

// Set timezone string according to <https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html>
#define TZ_INFO "EET-2EEST-3,M3.5.0,M10.5.0/3"

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Create your Data Point here
Point sensor("climate");




// Ca sa putem trimite un email folosing Gmail trebuie sa creem o parola folosind link: https://support.google.com/accounts/answer/185833
#define emailSenderAccount    "popa.robert.u3n@student.ucv.ro"
#define emailSenderPassword   "mleaywyingsarorl"
#define smtpServer            "smtp.gmail.com"
#define smtpServerPort        587
#define emailSubject          "[ALERT] Temperature"

// Adresa de email standard a destinatarului
String inputEmail = "poparobert999@gmail.com";
String enableEmailChecked = "checked";
String inputCheck = "true";
// Pragurile standard pentru limitle de temperatura
String inputTemp_H = "24.0";
String lastTemperature;
String inputTemp_L = "21.0";
//Perioada standard pentru citirea datelor
String inputInterval = "2000";

// Pagina HTML din care preluam configuratii personalizate
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>Email Notification with Temperature</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  </head><body>
  <h2>Temperatura incapere</h2> 
  <h3>%TEMPERATURE% &deg;C</h3>
  <h2>Alerta Email ESP32</h2>
  <form action="/get">
    Adresa de email <input type="email" name="email_input" value="%EMAIL_INPUT%" required><br>
    Notificare email <input type="checkbox" name="enable_email_input" value="true" %ENABLE_EMAIL%><br>
    Pregul superior <input type="number" step="0.1" name="threshold_top_input" value="%THRESHOLD_TOP%" required><br>
    Pragul inferior <input type="number" step="0.1" name="threshold_bot_input" value="%THRESHOLD_BOT%" required><br>
    Interval <input type="number" step="1000" name="interval_input" value="%INTERVAL%" required><br>
    <input type="submit" value="Submit">
  </form>
</body></html>)rawliteral";

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

AsyncWebServer server(80);

// Replaces placeholder with DHT11 values
String processor(const String& var){
  //Serial.println(var);
  if(var == "TEMPERATURE"){
    return lastTemperature;
  }
  else if(var == "EMAIL_INPUT"){
    return inputEmail;
  }
  else if(var == "ENABLE_EMAIL"){
    return enableEmailChecked;
  }
  else if(var == "THRESHOLD_TOP"){
    return inputTemp_H;
  }
   else if(var == "THRESHOLD_BOT"){
    return inputTemp_L;
  }
  else if(var == "INTERVAL");{
    return inputInterval;
  }
  return String();
}

// Folosim emailSent pe post de flag pentru a vedea daca email s-a trimis
bool emailSent = false;

const char* PARAM_INPUT_1 = "email_input";
const char* PARAM_INPUT_2 = "enable_email_input";
const char* PARAM_INPUT_3 = "threshold_top_input";
const char* PARAM_INPUT_4 = "threshold_bot_input";
const char* PARAM_INPUT_5 = "interval_input";
// Interval between sensor readings. Learn more about timers: https://RandomNerdTutorials.com/esp32-pir-motion-sensor-interrupts-timers/
unsigned long previousMillis = 0;     
 long interval = 2000;    



SMTPData smtpData;

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed!");
    return;
  }
  Serial.println();
  Serial.print("ESP IP Address: http://");
  Serial.println(WiFi.localIP());
  
  sensor.addTag("device", DEVICE);
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // Initalizam senzorul DHT11
  dht.begin();

  //Verificam conexiunea la server pentru InfluxDB
  if (client.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }

  // Trimitere pagina catre client
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  // Prelucrare HTTP GET request 
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
    // Preluare adresa email
    if (request->hasParam(PARAM_INPUT_1)) {
      inputEmail = request->getParam(PARAM_INPUT_1)->value();
      // Preluare conditie pentru trimitere email sau nu
      if (request->hasParam(PARAM_INPUT_2)) {
        inputCheck = request->getParam(PARAM_INPUT_2)->value();
        enableEmailChecked = "checked";
      }
      else {
        inputCheck = "false";
        enableEmailChecked = "";
      }
      // Preluare parametri pentru pragulile de temperatura
      if (request->hasParam(PARAM_INPUT_3)) {
        inputTemp_H = request->getParam(PARAM_INPUT_3)->value();
      }
      if (request->hasParam(PARAM_INPUT_4)) {
        inputTemp_L = request->getParam(PARAM_INPUT_4)->value();
      }
      //Preluare interval de procesare
      if(request->hasParam(PARAM_INPUT_5)){
        inputInterval= request->getParam(PARAM_INPUT_5)->value();
      }
    }
    else {
      inputEmail = "No message sent";
    }
    Serial.println(inputEmail);
    Serial.println(inputCheck);
    Serial.println(inputTemp_H);
    Serial.println(inputTemp_L);
    Serial.println(inputInterval);
    request->send(200, "text/html", "Parametri au fost trimisi catre ESP.<br><a href=\"/\">Return to Home Page</a>");
  });
  server.onNotFound(notFound);
  server.begin();
}

void loop() {
  sensor.clearFields();
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    
    previousMillis = currentMillis;
    interval=inputInterval.toInt();
    Serial.print("Interval : ");
    Serial.println(interval);
    // Temperature in Celsius degrees 
    float temperature = dht.readTemperature();
    float h = dht.readHumidity();
    
    
    sensor.addField("humidity", h);
    sensor.addField("temperature", temperature);
    
    Serial.print("Writing: ");
    Serial.println(sensor.toLineProtocol());

    // Write point
   if (!client.writePoint(sensor)) {
    Serial.print("Nereusire trimitere catre InfluxDB: ");
    Serial.println(client.getLastErrorMessage());
    }

    lastTemperature = String(temperature);
    
    // Varificam daca temperatura depaseste pragul impus si trimitem alerta pe mail
    if (temperature > inputTemp_H.toFloat() && inputCheck == "true" && !emailSent) {
        String emailMessage = String("Temperatura a depasit pragul setat. Temperatura curenta: ") + String(temperature) + String("*C");
        if (sendEmailNotification(emailMessage)) {
             Serial.println(emailMessage);
            emailSent = true;
        } 
          else {
            Serial.println("Esuare trimitere email");
            }
    }
    // Varificam daca temperatura scade sub pragul impus si trimitem alerta pe mail
    if (temperature < inputTemp_L.toFloat() && inputCheck == "true" && !emailSent) {
      String emailMessage = String("Temperatura a scazut sub pragul setat. Temperatura curenta: ") + String(temperature) + String("*C");
        if (sendEmailNotification(emailMessage)) {
          Serial.println(emailMessage);
          emailSent = true;
          }   
          else {
          Serial.println("Esuare trimitere email");
            }
      }


  }

 }

 bool sendEmailNotification(String emailMessage){
  // Setam SMTP Server Email host, port, account and password
  smtpData.setLogin(smtpServer, smtpServerPort, emailSenderAccount, emailSenderPassword);

  // Setam numele si adresa expediatorului
  smtpData.setSender("ESP32 Alert", emailSenderAccount);

  // Putem seta importanta email la trimitere cu  "High, Normal, Low sau 1 to 5 (1 este cel mai ridicat nivel)""
  smtpData.setPriority("High");

  // Subiectul mail-lui
  smtpData.setSubject(emailSubject);

  // Setam mesajul
  smtpData.setMessage(emailMessage, true);

  // adaugam destinatarul
  smtpData.addRecipient(inputEmail);

  smtpData.setSendCallback(sendCallback);

  // Trimitera email si folosirea functiei callback pentru verificarea statusului
  if (!MailClient.sendMail(smtpData)) {
    Serial.println("Error sending Email, " + MailClient.smtpErrorReason());
    return false;
  }
  
  smtpData.empty();
  return true;
 }

 // Functia Callback pentru statusul email
 void sendCallback(SendStatus msg) {
  // Print the current status
  Serial.println(msg.info());

  
  if (msg.success()) {
    Serial.println("----------------||-------------");
  }
}