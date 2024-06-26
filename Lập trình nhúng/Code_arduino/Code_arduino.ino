#include <DHT.h>

#define DHTPIN 4      
#define DHTTYPE DHT11 

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200); 
  dht.begin();
}

void loop() {
  Serial.flush();
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  int analogValue = analogRead(A0); // Đọc giá trị từ cảm biến MQ2

// Gửi dữ liệu qua UART
Serial.print(temperature);
Serial.print(",");
Serial.print(humidity);
Serial.print(",");
Serial.println(analogValue);
  delay(100);
}