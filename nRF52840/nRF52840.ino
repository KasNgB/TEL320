#include <ArduinoBLE.h>

BLEService dataService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEFloatCharacteristic dataChar("19B10001-E8F2-537E-4F6C-D104768A1214",
                                BLERead | BLENotify);

String rxLine = "";

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    while (1);
  }

  BLE.setLocalName("nRF52840-dBFS");
  BLE.setAdvertisedService(dataService);
  dataService.addCharacteristic(dataChar);
  BLE.addService(dataService);
  dataChar.writeValue(0.0f);
  BLE.advertise();

  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_BUILTIN, LOW);  delay(100);
    digitalWrite(LED_BUILTIN, HIGH); delay(100);
  }
}

uint8_t rxBuf[5];
uint8_t rxN = 0;

void loop() {
  BLE.poll();

  while (Serial1.available() > 0) {
    rxBuf[rxN++] = Serial1.read();
    if (rxN < 5) continue;

    if (rxBuf[0] != 0xAA) {            // not aligned
      memmove(rxBuf, rxBuf + 1, 4);    // slide by one byte
      rxN = 4;
      continue;
    }

    rxN = 0;
    float dbfs;
    memcpy(&dbfs, rxBuf + 1, 4);

    if (BLE.connected()) dataChar.writeValue(dbfs);
  }
}