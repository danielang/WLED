#pragma once

#include "wled.h"
#include <Wire.h>

#ifndef WLED_ENABLE_MQTT
    #error "This user mod requires MQTT to be enabled."
#endif

#define USERMOD_ID_COUCH_INTEGRATION 1095

#define PIN_BUTTON_LEFT 27
#define PIN_BUTTON_RIGHT 25

#define PIN_RELAY_1 32
#define PIN_RELAY_2 4

#define FOREWARD 1
#define BACKWARD -1

class UsermodCouchIntegration : public Usermod {
  private:
    unsigned long buttonLeftPressed = 0;
    unsigned long buttonRightPressed = 0;

    void checkButtons(unsigned long timestamp) {
      bool buttonLeft = !digitalRead(PIN_BUTTON_LEFT);
      bool buttonRight = !digitalRead(PIN_BUTTON_RIGHT);

      if (buttonLeft && !buttonLeftPressed) {
        buttonLeftPressed = timestamp;
      } else if (!buttonLeft && buttonLeftPressed) {
        buttonLeftPressed = 0;
      }

      if (buttonRight && !buttonRightPressed) {
        buttonRightPressed = timestamp;
      } else if (!buttonRight && buttonRightPressed) {
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

      pinMode(PIN_BUTTON_LEFT, INPUT);
      pinMode(PIN_BUTTON_RIGHT, INPUT);

      pinMode(PIN_RELAY_1, OUTPUT);
      pinMode(PIN_RELAY_2, OUTPUT);

      // self test
      // digitalWrite(PIN_RELAY_1, HIGH);
      // digitalWrite(PIN_RELAY_2, HIGH);

      // delay(750);

      // digitalWrite(PIN_RELAY_1, LOW);

      // delay(750);

      // digitalWrite(PIN_RELAY_1, HIGH);
      // digitalWrite(PIN_RELAY_2, LOW);

      // delay(750);

      // digitalWrite(PIN_RELAY_1, HIGH);
      // digitalWrite(PIN_RELAY_2, HIGH);
    }

    void loop() {
      unsigned long timestamp = millis();

      checkButtons(timestamp);

      calculateDirectionFromButton(timestamp);

      if (buttonDirection && (buttonLeftPressed || buttonRightPressed)) {
        direction = buttonDirection;

        if (!moveStartTimestamp) {
          moveStartTimestamp = timestamp;

          if (direction == FOREWARD) {
            digitalWrite(PIN_RELAY_1, LOW);
            digitalWrite(PIN_RELAY_2, HIGH);
          } else if (direction == BACKWARD) {
            digitalWrite(PIN_RELAY_1, HIGH);
            digitalWrite(PIN_RELAY_2, LOW);
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
      } else if (positionTarget != position) {
        int difference = positionTarget - position;
        direction = difference / abs(difference);

        Serial.println(direction);

        if (!moveStartTimestamp) {
          moveStartTimestamp = timestamp;

          if (direction == FOREWARD) {
            digitalWrite(PIN_RELAY_1, LOW);
            digitalWrite(PIN_RELAY_2, HIGH);
          } else if (direction == BACKWARD) {
            digitalWrite(PIN_RELAY_1, HIGH);
            digitalWrite(PIN_RELAY_2, LOW);
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
        digitalWrite(PIN_RELAY_1, HIGH);
        digitalWrite(PIN_RELAY_2, HIGH);
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