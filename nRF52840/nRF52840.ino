#include <ArduinoBLE.h>

// Information of how much data is being received and need to be stored
#define N_FLOATS    2                    
#define PAYLOAD_LEN (N_FLOATS * 4)       
#define FRAME_LEN   (1 + PAYLOAD_LEN) // THIS is  

// Setup for BLE
BLEService dataService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLECharacteristic dataChar("19B10001-E8F2-537E-4F6C-D104768A1214",
                           BLERead | BLENotify, PAYLOAD_LEN, true);

void setup() {
  Serial1.begin(115200); // Rx side of UART transmission
  pinMode(LED_BUILTIN, OUTPUT);

  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    while (1);
  }

  // More setup of BLE
  BLE.setLocalName("nRF52840-dBFS");
  BLE.setAdvertisedService(dataService);
  dataService.addCharacteristic(dataChar);
  BLE.addService(dataService);

  uint8_t zeros[PAYLOAD_LEN] = {0};          
  dataChar.writeValue(zeros, PAYLOAD_LEN); // Placeholder values, before real values are sent
  BLE.setConnectionInterval(0x0006, 0x000C); // Makes sure writeValue does not use too long SUPER IMPORTANT
  BLE.advertise();

  // Inidcate that startup has finished by blinking LED
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_BUILTIN, LOW);  delay(100);
    digitalWrite(LED_BUILTIN, HIGH); delay(100);
  }
}

// Receiving buffer. Size is 9 bytes, 1 magic byte and 2 * 4 data bytes
uint8_t rxBuf[FRAME_LEN];                    
uint8_t rxN = 0;

void loop() {
  BLE.poll();

  while (Serial1.available() > 0) {
    // Reads from UART, when rxN is = FRAME_LEN buffer is full 
    rxBuf[rxN++] = Serial1.read();
    if (rxN < FRAME_LEN) continue;          

    // When the buffer is ready to be sent magic byte should be in position 0.
    // If it is not in position 0 everything slides on byte to the left one more byte is read
    // This is repeated until the magic byte is in the right location
    if (rxBuf[0] != 0xAA) {                 
      memmove(rxBuf, rxBuf + 1, FRAME_LEN - 1);  
      rxN = FRAME_LEN - 1;                 
      continue;
    }

    // Last buffer has been sent, fill up new buffer
    rxN = 0;

    if (BLE.connected()) dataChar.writeValue(rxBuf + 1, PAYLOAD_LEN);   // both floats, one notification
  }
}