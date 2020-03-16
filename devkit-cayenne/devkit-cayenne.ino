/*******************************************************************************
 * Copyright (c) 2015 Thomas Telkamp and Matthijs Kooijman
 * Copyright (c) 2018 Terry Moore, MCCI
 *
 * Permission is hereby granted, free of charge, to anyone
 * obtaining a copy of this document and accompanying files,
 * to do whatever they want with them without any restriction,
 * including, but not limited to, copying, modification and redistribution.
 * NO WARRANTY OF ANY KIND IS PROVIDED.
 *
 * This example sends a valid LoRaWAN packet with payload "Hello,
 * world!", using frequency and encryption settings matching those of
 * the The Things Network. It's pre-configured for the Adafruit
 * Feather M0 LoRa.
 *
 *******************************************************************************/

/*******************************************************************************
 *
 * For Helium developers, follow the Arduino Quickstart guide:
 * https://developer.helium.com/device/arduino-quickstart
 * TLDR: register your device on the console:
 * https://console.helium.com/devices
 *
 * The App EUI (as lsb) and App Key (as msb) get inserted below.
 *
 *******************************************************************************/

#include <SPI.h>
#include <arduino_lmic.h>
#include <arduino_lmic_hal_boards.h>
#include <arduino_lmic_hal_configuration.h>
#include <arduino_lmic_lorawan_compliance.h>
#include <arduino_lmic_user_configuration.h>
#include <hal/hal.h>
#include <lmic.h>

#include <LSM6DSOSensor.h>
#include <LIS2DW12Sensor.h>
#include <LIS2MDLSensor.h>
#include <LPS22HHSensor.h>
#include <STTS751Sensor.h>
#include <HTS221Sensor.h>
#include <CayenneLPP.h>

#ifdef ARDUINO_SAM_DUE
#define DEV_I2C Wire1
#elif defined(ARDUINO_ARCH_STM32)
#define DEV_I2C Wire
#else
#define DEV_I2C Wire
#endif

// Sensors
LSM6DSOSensor *AccGyr;
LPS22HHSensor *PressTemp;
HTS221Sensor *HumTemp;

// This is the "App EUI" in Helium. Make sure it is little-endian (lsb).
static const u1_t PROGMEM APPEUI[8] = {FILL_ME_IN};
void os_getArtEui(u1_t *buf) { memcpy_P(buf, APPEUI, 8); }

// This should also be in little endian format
// These are user configurable values and Helium console permits anything
static const u1_t PROGMEM DEVEUI[8] = {FILL_ME_IN};
void os_getDevEui(u1_t *buf) { memcpy_P(buf, DEVEUI, 8); }

// This is the "App Key" in Helium. It is big-endian (msb).
static const u1_t PROGMEM APPKEY[16] = {FILL_ME_IN};
void os_getDevKey(u1_t *buf) { memcpy_P(buf, APPKEY, 16); }

CayenneLPP lpp(51);
static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 60;

// Pin mapping
//
// Adafruit BSPs are not consistent -- m0 express defs ARDUINO_SAMD_FEATHER_M0,
// m0 defs ADAFRUIT_FEATHER_M0
//
#if defined(ARDUINO_SAMD_FEATHER_M0) || defined(ADAFRUIT_FEATHER_M0)
// Pin mapping for Adafruit Feather M0 LoRa, etc.
const lmic_pinmap lmic_pins = {
    .nss = 8,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 4,
    .dio = {3, 6, LMIC_UNUSED_PIN},
    .rxtx_rx_active = 0,
    .rssi_cal = 8, // LBT cal for the Adafruit Feather M0 LoRa, in dB
    .spi_freq = 8000000,
};
#elif defined(ARDUINO_AVR_FEATHER32U4)
// Pin mapping for Adafruit Feather 32u4 LoRa, etc.
// Just like Feather M0 LoRa, but uses SPI at 1MHz; and that's only
// because MCCI doesn't have a test board; probably higher frequencies
// will work.
const lmic_pinmap lmic_pins = {
    .nss = 8,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 4,
    .dio = {7, 6, LMIC_UNUSED_PIN},
    .rxtx_rx_active = 0,
    .rssi_cal = 8, // LBT cal for the Adafruit Feather 32U4 LoRa, in dB
    .spi_freq = 1000000,
};
#elif defined(ARDUINO_CATENA_4551)
// Pin mapping for Murata module / Catena 4551
const lmic_pinmap lmic_pins = {
    .nss = 7,
    .rxtx = 29,
    .rst = 8,
    .dio =
        {
            25, // DIO0 (IRQ) is D25
            26, // DIO1 is D26
            27, // DIO2 is D27
        },
    .rxtx_rx_active = 1,
    .rssi_cal = 10,
    .spi_freq = 8000000 // 8MHz
};
#elif defined(MCCI_CATENA_4610)
#include "arduino_lmic_hal_boards.h"
const lmic_pinmap lmic_pins = *Arduino_LMIC::GetPinmap_Catena4610();
#elif defined(ARDUINO_DISCO_L072CZ_LRWAN1)
const lmic_pinmap lmic_pins = *Arduino_LMIC::GetPinmap_Disco_L072cz_Lrwan1();
#else
#error "Unknown target"
#endif

void onEvent(ev_t ev) {
  Serial.print(os_getTime());
  Serial.print(": ");
  switch (ev) {
  case EV_SCAN_TIMEOUT:
    Serial.println(F("EV_SCAN_TIMEOUT"));
    break;
  case EV_BEACON_FOUND:
    Serial.println(F("EV_BEACON_FOUND"));
    break;
  case EV_BEACON_MISSED:
    Serial.println(F("EV_BEACON_MISSED"));
    break;
  case EV_BEACON_TRACKED:
    Serial.println(F("EV_BEACON_TRACKED"));
    break;
  case EV_JOINING:
    Serial.println(F("EV_JOINING"));
    break;
  case EV_JOIN_TXCOMPLETE:
    Serial.println(F("EV_JOIN_TXCOMPLETE"));
    break;
  case EV_JOINED:
    Serial.println(F("EV_JOINED"));
    {
      u4_t netid = 0;
      devaddr_t devaddr = 0;
      u1_t nwkKey[16];
      u1_t artKey[16];
      LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
      Serial.print("netid: ");
      Serial.println(netid, DEC);
      Serial.print("devaddr: ");
      Serial.println(devaddr, HEX);
      Serial.print("artKey: ");
      for (size_t i = 0; i < sizeof(artKey); ++i) {
        if (i != 0)
          Serial.print("-");
        Serial.print(artKey[i], HEX);
      }
      Serial.println("");
      Serial.print("nwkKey: ");
      for (size_t i = 0; i < sizeof(nwkKey); ++i) {
        if (i != 0)
          Serial.print("-");
        Serial.print(nwkKey[i], HEX);
      }
      Serial.println("");
    }
    // Disable link check validation (automatically enabled
    // during join, but because slow data rates change max TX
    // size, we don't use it in this example.
    LMIC_setLinkCheckMode(0);
    break;
  /*
  || This event is defined but not used in the code. No
  || point in wasting codespace on it.
  ||
  || case EV_RFU1:
  ||     Serial.println(F("EV_RFU1"));
  ||     break;
  */
  case EV_JOIN_FAILED:
    Serial.println(F("EV_JOIN_FAILED"));
    break;
  case EV_REJOIN_FAILED:
    Serial.println(F("EV_REJOIN_FAILED"));
    break;
    break;
  case EV_TXCOMPLETE:
    Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
    if (LMIC.txrxFlags & TXRX_ACK)
      Serial.println(F("Received ack"));
    if (LMIC.dataLen) {
      Serial.println(F("Received "));
      Serial.println(LMIC.dataLen);
      Serial.println(F(" bytes of payload"));
    }
    // Schedule next transmission
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL),
                        do_send);
    break;
  case EV_LOST_TSYNC:
    Serial.println(F("EV_LOST_TSYNC"));
    break;
  case EV_RESET:
    Serial.println(F("EV_RESET"));
    break;
  case EV_RXCOMPLETE:
    // data received in ping slot
    Serial.println(F("EV_RXCOMPLETE"));
    break;
  case EV_LINK_DEAD:
    Serial.println(F("EV_LINK_DEAD"));
    break;
  case EV_LINK_ALIVE:
    Serial.println(F("EV_LINK_ALIVE"));
    break;
  /*
  || This event is defined but not used in the code. No
  || point in wasting codespace on it.
  ||
  || case EV_SCAN_FOUND:
  ||    Serial.println(F("EV_SCAN_FOUND"));
  ||    break;
  */
  case EV_TXSTART:
    Serial.println(F("EV_TXSTART"));
    break;
  default:
    Serial.print(F("Unknown event: "));
    Serial.println((unsigned)ev);
    break;
  }
}

void readSensors() {
  // Read humidity and temperature.
  float humidity = 0;
  float temperature = 0;
  HumTemp->GetHumidity(&humidity);
  HumTemp->GetTemperature(&temperature);

  // Read pressure and temperature.
  float pressure = 0;
  PressTemp->GetPressure(&pressure);

  // Read accelerometer and gyroscope.
  int32_t accelerometer[3];
  int32_t gyroscope[3];
  AccGyr->Get_X_Axes(accelerometer);
  AccGyr->Get_G_Axes(gyroscope);

  // Clear Payload
  lpp.reset();
  
  // Pack Packload
  lpp.addTemperature(1, temperature);
  lpp.addRelativeHumidity(2, humidity);
  lpp.addBarometricPressure(3, pressure); 
  lpp.addAccelerometer(4, accelerometer[0], accelerometer[1], accelerometer[2]);
  lpp.addGyrometer(5, gyroscope[0], gyroscope[1], gyroscope[2]);

  // Debug Print Data 
  Serial.print("| Hum[%]: ");
  Serial.print(humidity, 2);
  Serial.print(" | Temp[C]: ");
  Serial.print(temperature, 2);
  Serial.print(" | Pres[hPa]: ");
  Serial.print(pressure, 2);
  Serial.print(" | Acc[mg]: ");
  Serial.print(accelerometer[0]);
  Serial.print(" ");
  Serial.print(accelerometer[1]);
  Serial.print(" ");
  Serial.print(accelerometer[2]);
  Serial.print(" | Gyr[mdps]: ");
  Serial.print(gyroscope[0]);
  Serial.print(" ");
  Serial.print(gyroscope[1]);
  Serial.print(" ");
  Serial.print(gyroscope[2]);
}

void do_send(osjob_t *j) {
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, not sending"));
  } else {
    readSensors();
    // Prepare upstream data transmission at the next possible time.
    LMIC_setTxData2(1, lpp.getBuffer(), lpp.getSize(), 0);
    Serial.println(F("Packet queued"));
  }
  // Next TX is scheduled after TX_COMPLETE event.
}

void setup() {
  // Initialize I2C bus.
  DEV_I2C.begin();

  AccGyr = new LSM6DSOSensor (&DEV_I2C);
  AccGyr->Enable_X();
  AccGyr->Enable_G();
  PressTemp = new LPS22HHSensor(&DEV_I2C);
  PressTemp->Enable();
  HumTemp = new HTS221Sensor (&DEV_I2C);
  HumTemp->Enable();

  delay(5000);
  while (!Serial)
    ;
  Serial.begin(9600);
  Serial.println(F("Starting"));

#if defined(ARDUINO_DISCO_L072CZ_LRWAN1)
  SPI.setMOSI(RADIO_MOSI_PORT);
  SPI.setMISO(RADIO_MISO_PORT);
  SPI.setSCLK(RADIO_SCLK_PORT);
  SPI.setSSEL(RADIO_NSS_PORT);
// SPI.begin();
#endif

#ifdef VCC_ENABLE
  // For Pinoccio Scout boards
  pinMode(VCC_ENABLE, OUTPUT);
  digitalWrite(VCC_ENABLE, HIGH);
  delay(1000);
#endif

  // LMIC init
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  // allow much more clock error than the X/1000 default. See:
  // https://github.com/mcci-catena/arduino-lorawan/issues/74#issuecomment-462171974
  // https://github.com/mcci-catena/arduino-lmic/commit/42da75b56#diff-16d75524a9920f5d043fe731a27cf85aL633
  // the X/1000 means an error rate of 0.1%; the above issue discusses using
  // values up to 10%. so, values from 10 (10% error, the most lax) to 1000
  // (0.1% error, the most strict) can be used.
  LMIC_setClockError(1 * MAX_CLOCK_ERROR / 40);

  LMIC_setLinkCheckMode(0);
  LMIC_setDrTxpow(DR_SF8, 20);
  LMIC_selectSubBand(6);

  // Start job (sending automatically starts OTAA too)
  do_send(&sendjob);
}

void loop() { os_runloop_once(); }

namespace Arduino_LMIC {

class HalConfiguration_Disco_L072cz_Lrwan1_t : public HalConfiguration_t {
public:
  enum DIGITAL_PINS : uint8_t {
    PIN_SX1276_NSS = 37,
    PIN_SX1276_NRESET = 33,
    PIN_SX1276_DIO0 = 38,
    PIN_SX1276_DIO1 = 39,
    PIN_SX1276_DIO2 = 40,
    PIN_SX1276_RXTX = 21,
  };

  virtual bool queryUsingTcxo(void) override { return false; };
};
// save some typing by bringing the pin numbers into scope
static HalConfiguration_Disco_L072cz_Lrwan1_t myConfig;

static const HalPinmap_t myPinmap = {
    .nss = HalConfiguration_Disco_L072cz_Lrwan1_t::PIN_SX1276_NSS,
    .rxtx = HalConfiguration_Disco_L072cz_Lrwan1_t::PIN_SX1276_RXTX,
    .rst = HalConfiguration_Disco_L072cz_Lrwan1_t::PIN_SX1276_NRESET,

    .dio =
        {
            HalConfiguration_Disco_L072cz_Lrwan1_t::PIN_SX1276_DIO0,
            HalConfiguration_Disco_L072cz_Lrwan1_t::PIN_SX1276_DIO1,
            HalConfiguration_Disco_L072cz_Lrwan1_t::PIN_SX1276_DIO2,
        },
    .rxtx_rx_active = 1,
    .rssi_cal = 10,
    .spi_freq = 8000000, /* 8MHz */
    .pConfig = &myConfig};

}; // end namespace Arduino_LMIC
