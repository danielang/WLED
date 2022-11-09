#pragma once

#include "wled.h"
#include <Wire.h>
#include <MCP23017.h>

#define MCP23017_ADDRESS 0x27
#define MCP23017_INTERRUPT_PIN 18

#ifndef WLED_ENABLE_MQTT
    #error "This user mod requires MQTT to be enabled."
#endif

#define USERMOD_ID_COUCH_INTEGRATION 1095

#define I2C_SDA 19
#define I2C_SCL 23

#define FOREWARD 1
#define BACKWARD -1

#define BUTTON_LEFT 1
#define BUTTON_RIGHT 2

#define STEPPER_OFF 255
#define STEPPER_FOREWARD 64  // prev 0
#define STEPPER_BACKWARD 128

volatile bool interrupted = false;

void IRAM_ATTR userInterruptHandler() {
  interrupted = true;
}

class UsermodCouchIntegration : public Usermod {
  private:
    MCP23017 mcp = MCP23017(MCP23017_ADDRESS, Wire);

    unsigned long buttonLastCheck = 0;
    byte buttonInputState;
    unsigned long buttonLeftPressed = 0;
    unsigned long buttonRightPressed = 0;

    void checkButtons(unsigned long timestamp) {
      if (timestamp - buttonLastCheck < 250) {
        return; 
      }

      buttonLastCheck = timestamp;

      // Address A: 0x12
      // Address B: 0x13
      Wire.beginTransmission(0x27);
      Wire.write(0x13);
      Wire.endTransmission();

      // request one byte of data from MCP20317
      Wire.requestFrom(0x27, 1);

      buttonInputState = Wire.read();

      if (buttonInputState == BUTTON_LEFT + BUTTON_RIGHT && !(buttonLeftPressed && buttonRightPressed)) {
        buttonLeftPressed = timestamp;
        buttonRightPressed = timestamp;
      } else if (buttonInputState == BUTTON_LEFT && !buttonLeftPressed) {
        buttonLeftPressed = timestamp;
        buttonRightPressed = 0;
      } else if (buttonInputState == BUTTON_RIGHT && !buttonRightPressed) {
        buttonLeftPressed = 0;
        buttonRightPressed = timestamp;
      } else if (!buttonInputState || buttonInputState > BUTTON_LEFT + BUTTON_RIGHT) {
        buttonLeftPressed = 0;
        buttonRightPressed = 0;
      }
    }



    int buttonDirection = 0;

    void calculateDirectionFromButton(unsigned long timestamp) {
      if (buttonLeftPressed && !buttonRightPressed && buttonLeftPressed + 3000 < timestamp && !buttonDirection) {
        buttonDirection = FOREWARD;

        // start moving
        Serial.println("I like to move forewards");

      } else if (buttonRightPressed && !buttonLeftPressed && buttonRightPressed + 3000 < timestamp && !buttonDirection) {
        buttonDirection = BACKWARD;
        
        // start moving
        Serial.println("I like to move backwards");

      } else if (
        (!buttonLeftPressed && buttonDirection == FOREWARD) || (!buttonRightPressed && buttonDirection == BACKWARD) ||
        (buttonLeftPressed && buttonDirection == BACKWARD) || (buttonRightPressed && buttonDirection == FOREWARD) 
      ) {
        // stop moving
        Serial.println("STOP");
        buttonDirection = 0;
      }
    }



    uint8_t position = 0; // value between 0% and 100%
    uint8_t positionTarget = 0; // value between 0% and 100%
    int timePerPositionStep = 100; // 100ms for 1%

    int direction = 0;
    int previousDirection = 0;

    unsigned long moveStartTimestamp = 0;
    unsigned long moveLastTickTimestamp = 0;
    
  public:
    void setup() {
      Wire.setClock(10000);
      Wire.begin(I2C_SDA, I2C_SCL);

      mcp.init();
      mcp.portMode(MCP23017Port::A, 0);
      mcp.portMode(MCP23017Port::B, 0b11111111);

      mcp.interruptMode(MCP23017InterruptMode::Separated);
      mcp.interrupt(MCP23017Port::B, FALLING);

      mcp.writeRegister(MCP23017Register::IPOL_A, 0x00);
      mcp.writeRegister(MCP23017Register::IPOL_B, 0x00);

      mcp.writeRegister(MCP23017Register::GPIO_A, 0x00);
      mcp.writeRegister(MCP23017Register::GPIO_B, 0x00);

      mcp.clearInterrupts();

      pinMode(MCP23017_INTERRUPT_PIN, INPUT_PULLUP);
      attachInterrupt(MCP23017_INTERRUPT_PIN, userInterruptHandler, FALLING);

      // self test
      mcp.writePort(MCP23017Port::A, 0b10000000);
      delay(750);
      mcp.writePort(MCP23017Port::A, 0b01000000);
      delay(750);
      mcp.writePort(MCP23017Port::A, 0b11111111);


      // Wire.beginTransmission(0x27);
      // Wire.write(0x00); // IODIRA register
      // Wire.write(0x00); // set all of port A to outputs
      // Wire.endTransmission();

      // // switch off
      // Wire.beginTransmission(0x27);
      // Wire.write(0x12); // address port A
      // Wire.write(STEPPER_OFF);
      // Wire.endTransmission();
    }

    void loop() {
      unsigned long timestamp = millis();

      Serial.println("Loop");

      if (!interrupted) {
        mcp.clearInterrupts();

        mcp.writePort(MCP23017Port::A, 0b01000000);
        delay(750);
        mcp.writePort(MCP23017Port::A, 0b11111111);

        return;
      }

      delay(100);
      interrupted = false;
      mcp.clearInterrupts();

      mcp.writePort(MCP23017Port::A, 0b00000000);
      delay(750);
      mcp.writePort(MCP23017Port::A, 0b11111111);

      return;

      // checkButtons(timestamp);

      calculateDirectionFromButton(timestamp);

      if (buttonDirection && (buttonLeftPressed || buttonRightPressed)) {
        direction = buttonDirection;

        if (!moveStartTimestamp) {
          moveStartTimestamp = timestamp;

          if (direction == FOREWARD) {
            Wire.beginTransmission(0x27);
            Wire.write(0x12); // address port A
            Wire.write(STEPPER_FOREWARD);
            Wire.endTransmission();
          } else if (direction == BACKWARD) {
            Wire.beginTransmission(0x27);
            Wire.write(0x12); // address port A
            Wire.write(STEPPER_BACKWARD);
            Wire.endTransmission();
          }
        }

        if (timestamp - moveLastTickTimestamp > timePerPositionStep) {
          moveLastTickTimestamp = timestamp;

          if ((positionTarget >= 0 && positionTarget < 100 && direction > 0) || (positionTarget > 0 && positionTarget <= 100 && direction < 0)) {
            positionTarget += direction;

            position = positionTarget;

            publishMqttPosition(position);
          }
        }

        Serial.print("Position: ");
        Serial.println(position);
      } else if (positionTarget != position) {
        int difference = positionTarget - position;
        direction = difference / abs(difference);

        Serial.println(direction);

        if (!moveStartTimestamp) {
          moveStartTimestamp = timestamp;

          if (direction == FOREWARD) {
            Wire.beginTransmission(0x27);
            Wire.write(0x12); // address port A
            Wire.write(STEPPER_FOREWARD);
            Wire.endTransmission();
          } else if (direction == BACKWARD) {
            Wire.beginTransmission(0x27);
            Wire.write(0x12); // address port A
            Wire.write(STEPPER_BACKWARD);
            Wire.endTransmission();
          }
        }

        if (timestamp - moveLastTickTimestamp > timePerPositionStep) {
          moveLastTickTimestamp = timestamp;

          position += direction;

          publishMqttPosition(position);
        }
        
      } else if (moveStartTimestamp) {
        moveStartTimestamp = 0;
        moveLastTickTimestamp = 0;

        // stop motor
        Wire.beginTransmission(0x27);
        Wire.write(0x12); // address port A
        Wire.write(STEPPER_OFF);
        Wire.endTransmission();
      }

    }

    void onMqttConnect(bool sessionPresent) {
      // (re)subscribe to required topics
      char subuf[64];
      if (mqttDeviceTopic[0] != 0) {
        strcpy(subuf, mqttDeviceTopic);
        strcat_P(subuf, PSTR("/motor/set"));
        mqtt->subscribe(subuf, 0);

        Serial.print("Subscribed to: ");
        Serial.println(subuf);

        // publish current state
        publishMqttPosition(position);
      }
    }

    bool onMqttMessage(char* topic, char* payload) {
      if (strlen(topic) == 10 && strncmp_P(topic, PSTR("/motor/set"), 10) == 0) {
        uint8_t mqttPositionTarget = strtoul(payload, NULL, 10);

        if (mqttPositionTarget > 100) {
          // accept only values between 0 & 100
          return false;
        }

        Serial.print("[mqtt] set positionTarget to ");
        Serial.println(mqttPositionTarget);

        positionTarget = mqttPositionTarget;

        return true;
      }

      return false;
    }

    void publishMqttPosition(const uint8_t position) {
      //Check if MQTT Connected, otherwise it will crash the 8266
      if (WLED_MQTT_CONNECTED) {
        char payload[16];
        sprintf(payload, "%d", position);

        char subuf[64];
        sprintf_P(subuf, PSTR("%s/motor"), mqttDeviceTopic);
        mqtt->publish(subuf, 0, false, payload);
      }
    }

    uint16_t getId() {
      return USERMOD_ID_COUCH_INTEGRATION;
    }
};