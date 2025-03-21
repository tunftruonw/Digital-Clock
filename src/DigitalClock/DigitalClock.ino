#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include "RTClib.h"
#include <ArduinoJson.h>

// Declare RTC DS1307 object to get real-time data
RTC_DS1307 rtc;

// Declare I2C LCD object with address 0x27, size 16x2
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Analog pin to read LM35 temperature sensor
const int lm35_pin = A0;

// GPIO D3 pin to control the buzzer
const int buzzer_pin = D3;

// Degree symbol (°C) for LCD display
char degree = 223;

// Array of days of the week in English
char daysOfTheWeek[7][12] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

// Initialize web server on port 80
ESP8266WebServer server(80);

// API Key from OpenWeatherMap to fetch Hanoi weather data
const char* apiKey = "556a9634ddb51d04a9da316bc1ecd5a0";

// Structure to store alarm information
struct Alarm {
  int hour;                     // Alarm hour
  int minute;                   // Alarm minute
  bool triggered;               // Whether the alarm has been triggered
  unsigned long buzzStartTime;  // Time when buzzer starts (for millis)
};
Alarm alarms[10];                   // Array to store up to 10 alarms
int alarmCount = 0;                 // Current number of alarms
const int buzzerDuration = 300000;  // Fixed buzzer duration: 5 minutes (300,000 ms)
bool buzzerActive = false;          // Flag to track if buzzer is currently active

// Variables for timing LM35 readings using millis()
unsigned long timePreMillis = lm35PreMillis = 0;  // Store the last time LM35 was read
const long lm35Interval = 60000;                  // Interval for reading LM35 (60 seconds)
const long timeInterval = 1000;
bool isTheFirstDisplay = true;  // Flag for initial temperature display

// HTML string for the web interface
String webPage = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Digital Clock</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            background-color: #f0f0f0;
            padding: 20px;
        }
        .container {
            max-width: 600px;
            margin: 0 auto;
            background: white;
            padding: 20px;
            border-radius: 10px;
            gap: 10px
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        #clock {
            font-size: 48px;
            margin: 20px 0;
        }
        #weather {
            font-size: 24px;
            margin: 20px 0;
        }
        #alarmList {
            margin: 20px 0;
        }
        .alarm-item {
            margin: 10px 0;
            padding: 10px;
            background: #f9f9f9;
            border-radius: 5px;
            display: flex;
            justify-content: center;
            align-items: center;
        }
        #stopButton {
            display: none; /* Hidden by default */
            margin: 20px auto;
            padding: 10px 20px;
            background-color: #ff4444;
            color: white;
            border: none;
            border-radius: 5px;
            cursor: pointer;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Digital Clock</h1>
        <div id="clock">Loading time...</div>
        <div id="weather">Loading weather data...</div>
        
        <h3>Set Alarm</h3>
        <input type="time" id="alarmTime">
        <button onclick="setAlarm()">Set Alarm</button>
        
        <button id="stopButton" onclick="stopAlarm()">Stop Alarm</button>
        
        <h3>Alarm List</h3>
        <div id="alarmList"></div>
    </div>

    <script>
        function updateClock() {
            fetch('/time')
                .then(response => response.text())
                .then(data => document.getElementById('clock').innerHTML = data)
                .catch(error => console.error('Error:', error));
        }
        setInterval(updateClock, 1000);
        updateClock();

        function updateWeather() {
            fetch('/weather')
                .then(response => response.text())
                .then(data => document.getElementById('weather').textContent = data)
                .catch(error => console.error('Error:', error));
        }
        setInterval(updateWeather, 60000);
        updateWeather();

        function checkBuzzerState() {
            fetch('/buzzerState')
                .then(response => response.text())
                .then(data => {
                    const stopButton = document.getElementById('stopButton');
                    if (data === 'active') {
                        stopButton.style.display = 'block';
                    } else {
                        stopButton.style.display = 'none';
                    }
                })
                .catch(error => console.error('Error checking buzzer state:', error));
        }
        setInterval(checkBuzzerState, 1000);

        let alarms = [];
        function setAlarm() {
            const alarmTime = document.getElementById('alarmTime').value;
            if (!alarmTime) {
                alert('Please select a time!');
                return;
            }
            const [hours, minutes] = alarmTime.split(':');
            alarms.push({ hours, minutes });
            updateAlarmList();

            fetch('/setAlarm?hour=' + hours + '&minute=' + minutes, { method: 'POST' })
                .then(response => response.text())
                .then(data => console.log(data))
                .catch(error => console.error('Error sending alarm:', error));
            
            document.getElementById('alarmTime').value = '';
        }

        function deleteAlarm(hour, minute) {
            alarms = alarms.filter(alarm => alarm.hours != hour || alarm.minutes != minute);
            updateAlarmList();
            fetch('/deleteAlarm?hour=' + hour + '&minute=' + minute, { method: 'POST' })
                .then(response => response.text())
                .then(data => console.log(data))
                .catch(error => console.error('Error deleting alarm:', error));
        }

        function editAlarm(oldHour, oldMinute) {
            const newTime = prompt('Enter new time (HH:MM):', `${oldHour}:${oldMinute}`);
            if (newTime) {
                const [newHour, newMinute] = newTime.split(':');
                if (newHour && newMinute) {
                    fetch('/editAlarm?oldHour=' + oldHour + '&oldMinute=' + oldMinute + '&newHour=' + newHour + '&newMinute=' + newMinute, { method: 'POST' })
                        .then(response => response.text())
                        .then(data => {
                            console.log(data);
                            alarms = alarms.map(alarm => 
                                alarm.hours == oldHour && alarm.minutes == oldMinute 
                                ? { hours: newHour, minutes: newMinute } 
                                : alarm
                            );
                            updateAlarmList();
                        })
                        .catch(error => console.error('Error editing alarm:', error));
                } else {
                    alert('Invalid time format!');
                }
            }
        }

        function stopAlarm() {
            fetch('/stopAlarm', { method: 'POST' })
                .then(response => response.text())
                .then(data => {
                    console.log(data);
                    document.getElementById('stopButton').style.display = 'none';
                })
                .catch(error => console.error('Error stopping alarm:', error));
        }

        function updateAlarmList() {
            const alarmList = document.getElementById('alarmList');
            alarmList.innerHTML = '';
            alarms.forEach((alarm, index) => {
                const div = document.createElement('div');
                div.className = 'alarm-item';
                div.textContent = `Alarm: ${alarm.hours}:${alarm.minutes}`;
                const editBtn = document.createElement('button');
                editBtn.textContent = 'Edit';
                editBtn.onclick = () => editAlarm(alarm.hours, alarm.minutes);
                const deleteBtn = document.createElement('button');
                deleteBtn.textContent = 'Delete';
                deleteBtn.onclick = () => deleteAlarm(alarm.hours, alarm.minutes);
                div.appendChild(editBtn);
                div.appendChild(deleteBtn);
                alarmList.appendChild(div);
            });
        }
    </script>
</body>
</html>
)=====";

void setup() {
  Serial.begin(115200);           // Initialize Serial for debugging
  Wire.begin(D2, D1);             // Initialize I2C with SDA=D2, SCL=D1
  pinMode(buzzer_pin, OUTPUT);    // Configure D3 as output for buzzer
  digitalWrite(buzzer_pin, LOW);  // Turn off buzzer initially

  // Initialize WiFiManager to connect to WiFi
  WiFiManager wm;
  bool res = wm.autoConnect("DigitalClock", "12345678");  // Create AP with name and password

  lcd.init();           // Initialize LCD
  lcd.backlight();      // Turn on LCD backlight
  lcd.setCursor(1, 0);  // Set cursor to start of line 1
  lcd.print("DIGITAL CLOCK");
  delay(5000);

  if (!res) {  // If WiFi connection fails
    lcd.print("Failed to connect");
    Serial.println("Failed to connect");
    while (1) delay(1000);  // Halt program
  } else {                  // If connection succeeds
    lcd.clear();
    lcd.print("Connected, IP:");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());  // Display IP on LCD
    Serial.println("connected...yeey :)");
    Serial.println("IP: " + WiFi.localIP().toString());
    delay(5000);
  }

  lcd.clear();

  if (!rtc.begin()) {  // If RTC is not found
    lcd.clear();
    lcd.print("Couldn't find RTC");
    Serial.println("Couldn't find RTC");
    while (1) delay(1000);
  }
  if (!rtc.isrunning()) {  // If RTC is not running
    lcd.clear();
    lcd.print("RTC is NOT running!");
    Serial.println("RTC is NOT running!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // Set time from computer
  }
  lcd.clear();

  // Register endpoints for web server
  server.on("/", handleRoot);
  server.on("/time", handleTime);
  server.on("/weather", handleWeather);
  server.on("/setAlarm", handleAlarm);
  server.on("/deleteAlarm", handleDeleteAlarm);
  server.on("/editAlarm", handleEditAlarm);
  server.on("/stopAlarm", handleStopAlarm);
  server.on("/buzzerState", handleBuzzerState);
  server.begin();  // Start the server
  Serial.println("HTTP server started at " + WiFi.localIP().toString());
}

void loop() {
  displayDate();          // Display date on LCD
  displayTime();          // Display time on LCD
  checkTempState();       // Check the first time and display temperature every 60s
  server.handleClient();  // Handle incoming client requests
  checkAlarms();          // Check and trigger alarms
}

// Function to read temperature from LM35 sensor
int getTemperature() {
  int temp_adc_val = analogRead(lm35_pin);      // Read ADC value
  float voltage = temp_adc_val * 5.0 / 1024.0;  // Convert to voltage
  int temp_value = voltage * 100;               // Convert to temperature (°C)
  return temp_value;
}

// Function to display temperature on LCD
void displayTemperature() {
  lcd.setCursor(11, 1);
  lcd.print(getTemperature());
  lcd.print(degree);
  lcd.print("C");
}

void checkTempState() {
  if (isTheFirstDisplay) {
    displayTemperature();
    isTheFirstDisplay = false;
  }
  // Use millis() to read LM35 every minute
  unsigned long currentMillis = millis();
  if (currentMillis - lm35PreMillis >= lm35Interval) {
    lm35PreMillis = currentMillis;  // Update the last reading time
    displayTemperature();           // Display temperature on LCD
  }
}

// Function to display time on LCD
void displayTime() {
  unsigned long currentMillis = millis();
  if (currentMillis - timePreMillis >= timeInterval) {
    timePreMillis = currentMillis;
    DateTime now = rtc.now();
    lcd.setCursor(1, 1);
    if (now.hour() <= 9) lcd.print("0");
    lcd.print(now.hour());
    lcd.print(':');
    if (now.minute() <= 9) lcd.print("0");
    lcd.print(now.minute());
    lcd.print(':');
    if (now.second() <= 9) lcd.print("0");
    lcd.print(now.second());
    lcd.print(" ");
  }
}

// Function to display date on LCD
void displayDate() {
  DateTime now = rtc.now();
  if (now.hour == 0) {
    lcd.setCursor(1, 0);  // Set cursor to column 1, line 1
    lcd.print(daysOfTheWeek[now.dayOfTheWeek()]);
    lcd.print(",");
    if (now.day() <= 9) lcd.print("0");
    lcd.print(now.day());
    lcd.print('/');
    if (now.month() <= 9) lcd.print("0");
    lcd.print(now.month());
    lcd.print('/');
    lcd.print(now.year());
  }
}

// Handler for root endpoint to serve the webpage
void handleRoot() {
  server.send(200, "text/html; charset=UTF-8", webPage);
}

// Handler for time endpoint to send current time
void handleTime() {
  DateTime now = rtc.now();
  char clockStr[30];
  sprintf(clockStr, "%02d/%02d/%04d<br>%02d:%02d:%02d",
          now.day(), now.month(), now.year(),
          now.hour(), now.minute(), now.second());
  server.send(200, "text/html; charset=UTF-8", clockStr);
}

// Handler for weather endpoint to fetch Hanoi weather
void handleWeather() {
  if (WiFi.status() != WL_CONNECTED) {  // Check WiFi connection
    server.send(200, "text/plain; charset=UTF-8", "No WiFi connection");
    return;
  }

  WiFiClient client;
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=Hanoi&appid=";
  url += apiKey;
  url += "&units=metric&lang=en";

  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode > 0) {  // If request succeeds
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);

    float temp = doc["main"]["temp"];
    const char* description = doc["weather"][0]["description"];

    char weatherStr[50];
    sprintf(weatherStr, "Hanoi: %.1f°C - %s", temp, description);
    server.send(200, "text/plain; charset=UTF-8", weatherStr);
  } else {
    server.send(200, "text/plain; charset=UTF-8", "Error loading weather data");
  }
  http.end();
}

// Handler for setting an alarm
void handleAlarm() {
  if (server.hasArg("hour") && server.hasArg("minute")) {
    int hour = server.arg("hour").toInt();
    int minute = server.arg("minute").toInt();

    if (alarmCount < 10) {                              // Check if alarm list is not full
      alarms[alarmCount] = { hour, minute, false, 0 };  // Initialize with buzzStartTime = 0
      alarmCount++;
      server.send(200, "text/plain", "Alarm set: " + String(hour) + ":" + String(minute));
      Serial.println("Alarm set: " + String(hour) + ":" + String(minute));
    } else {
      server.send(400, "text/plain", "Alarm list is full!");
    }
  } else {
    server.send(400, "text/plain", "Missing hour or minute parameter");
  }
}

// Handler for deleting an alarm
void handleDeleteAlarm() {
  if (server.hasArg("hour") && server.hasArg("minute")) {
    int hour = server.arg("hour").toInt();
    int minute = server.arg("minute").toInt();

    for (int i = 0; i < alarmCount; i++) {
      if (alarms[i].hour == hour && alarms[i].minute == minute) {  // Find matching alarm
        for (int j = i; j < alarmCount - 1; j++) {
          alarms[j] = alarms[j + 1];  // Shift elements to remove alarm
        }
        alarmCount--;
        server.send(200, "text/plain", "Alarm deleted: " + String(hour) + ":" + String(minute));
        Serial.println("Alarm deleted: " + String(hour) + ":" + String(minute));
        return;
      }
    }
    server.send(404, "text/plain", "Alarm not found: " + String(hour) + ":" + String(minute));
  } else {
    server.send(400, "text/plain", "Missing hour or minute parameter");
  }
}

// Handler for editing an alarm
void handleEditAlarm() {
  if (server.hasArg("oldHour") && server.hasArg("oldMinute") && server.hasArg("newHour") && server.hasArg("newMinute")) {
    int oldHour = server.arg("oldHour").toInt();
    int oldMinute = server.arg("oldMinute").toInt();
    int newHour = server.arg("newHour").toInt();
    int newMinute = server.arg("newMinute").toInt();

    for (int i = 0; i < alarmCount; i++) {
      if (alarms[i].hour == oldHour && alarms[i].minute == oldMinute) {  // Find matching alarm
        alarms[i].hour = newHour;
        alarms[i].minute = newMinute;
        alarms[i].triggered = false;  // Reset triggered state for new time
        alarms[i].buzzStartTime = 0;  // Reset buzz start time
        server.send(200, "text/plain", "Alarm edited: " + String(oldHour) + ":" + String(oldMinute) + " to " + String(newHour) + ":" + String(newMinute));
        Serial.println("Alarm edited: " + String(oldHour) + ":" + String(oldMinute) + " to " + String(newHour) + ":" + String(newMinute));
        return;
      }
    }
    server.send(404, "text/plain", "Alarm not found: " + String(oldHour) + ":" + String(oldMinute));
  } else {
    server.send(400, "text/plain", "Missing oldHour, oldMinute, newHour, or newMinute parameter");
  }
}

// Handler to stop the buzzer
void handleStopAlarm() {
  if (buzzerActive) {
    digitalWrite(buzzer_pin, LOW);  // Turn off buzzer
    buzzerActive = false;           // Reset buzzer state
    for (int i = 0; i < alarmCount; i++) {
      if (alarms[i].buzzStartTime > 0) {
        alarms[i].buzzStartTime = 0;  // Reset buzz start time for active alarm
      }
    }
    server.send(200, "text/plain", "Alarm stopped");
    Serial.println("Alarm stopped manually");
  } else {
    server.send(200, "text/plain", "No active alarm");
  }
}

// Handler to check buzzer state
void handleBuzzerState() {
  if (buzzerActive) {
    server.send(200, "text/plain", "active");
  } else {
    server.send(200, "text/plain", "inactive");
  }
}

// Function to check and trigger alarms
void checkAlarms() {
  DateTime now = rtc.now();
  int currentHour = now.hour();
  int currentMinute = now.minute();
  int currentSecond = now.second();
  unsigned long currentMillis = millis();

  for (int i = 0; i < alarmCount; i++) {
    if (!alarms[i].triggered && alarms[i].hour == currentHour && alarms[i].minute == currentMinute && currentSecond < 5) {
      Serial.println("Alarm triggered: " + String(alarms[i].hour) + ":" + String(alarms[i].minute));
      digitalWrite(buzzer_pin, HIGH);           // Turn on buzzer
      buzzerActive = true;                      // Mark buzzer as active
      alarms[i].buzzStartTime = currentMillis;  // Record start time
      alarms[i].triggered = true;               // Mark alarm as triggered (but not removed)
    }

    // Check if buzzer should stop based on duration
    if (buzzerActive && alarms[i].buzzStartTime > 0 && (currentMillis - alarms[i].buzzStartTime >= (unsigned long)buzzerDuration)) {
      digitalWrite(buzzer_pin, LOW);  // Turn off buzzer after duration
      buzzerActive = false;           // Reset buzzer state
      alarms[i].buzzStartTime = 0;    // Reset start time
      // Note: Alarm remains in the list, not deleted
    }
  }
}