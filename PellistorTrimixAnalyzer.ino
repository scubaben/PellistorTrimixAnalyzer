/*Copyright (c) 2017 Ben Shiner
Special thanks to JJ Crawford for his technical guidance and numerous contributions

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <Wire.h> // wiring library
#include <LiquidCrystal.h> //LCD library
#include <EEPROM.h> //eeprom library for saving calibration data
#include "Sensor.h"

#define VERSION "0.1"

#define buttonPin A3
#define encoderPinA 0
#define encoderPinB 1
#define batteryPin A9

LiquidCrystal lcd(13, 12, 11, 10, 6, 5); //create LCD object, these pins are the ones I chose to use on the adafruit feather 32u4 proto board

int buttonState;
int lastButtonState = HIGH;
unsigned long lastSampleMillis = millis();
unsigned long lastDisplayMillis = millis();
unsigned long sampleRate = 400;
unsigned long debounceMillis = 0;
unsigned long debounceDelay = 50;
int displayMode = 0;
boolean updateRightDisplay = false;

//use volatie variables when they get changed by an ISR (interrupt service routine)
volatile bool aCurrentState;
volatile bool bCurrentState;
volatile bool aPreviousState;
volatile bool bPreviousState;
volatile int currentSetting;
volatile int encoderTicks;

//handy character designer: https://www.quinapalus.com/hd44780udg.html
byte thickSeparator[8] = { B1010, B100, B1010, B100, B1010, B100, B1010 };
byte thinSeparator[8] = { B100, B0, B100, B0, B100, B0, B100 };
byte arrowRight[8] = { B0, B1000, B1100, B1110, B1100, B1000, B0 };
byte arrowLeft[8] = { B0, B10, B110, B1110, B110, B10, B0 };
byte oTwo[8] = { B11, B1, B11110, B10111, B10100, B10100, B11100 };

Sensor sensor1(0, OXYGEN);
Sensor sensor2(1, HELIUM);

void setup() {
	pinMode(buttonPin, INPUT_PULLUP);
	pinMode(encoderPinA, INPUT_PULLUP);
	pinMode(encoderPinB, INPUT_PULLUP);

	attachInterrupt(digitalPinToInterrupt(encoderPinA), aEncoderInterrupt, CHANGE);
	attachInterrupt(digitalPinToInterrupt(encoderPinB), bEncoderInterrupt, CHANGE);

	lcd.begin(16, 2);
	lcd.createChar(0, thickSeparator);
	lcd.createChar(1, thinSeparator);
	lcd.createChar(2, arrowRight);
	lcd.createChar(3, arrowLeft);
	lcd.createChar(4, oTwo);

	lcd.setCursor(0, 0);
	lcd.print("Trimix Analyzer");
	lcd.setCursor(0, 1);
	lcd.print(getVoltage());
	lcd.print("v");
	lcd.setCursor(11, 1);
	lcd.print("v ");
	lcd.print(VERSION);
	delay(2000);
	lcd.clear();

	if (digitalRead(buttonPin) == LOW) {
		calibrate();
	}
	else if (!sensor1.isCalibrated() || !sensor2.isCalibrated()) {
		lcd.clear();
		lcd.print("Please");
		lcd.setCursor(0, 1);
		lcd.print("Recalibrate");
		delay(2000);
		calibrate();
	}
	lcd.clear();
}

void loop() {
	displayGas();
	displayRight();
	if (buttonDetect(buttonPin)) {
		optionsMenu();
	}
}

//uses edge detection and debouncing to detect button pushes
boolean buttonDetect(int detectPin) {
	boolean buttonPushed = false;
	buttonState = digitalRead(detectPin);
	if ((buttonState != lastButtonState) && buttonState == LOW) {
		debounceMillis = millis();
		lastButtonState = buttonState;
	}
	if (((millis() - debounceMillis) > debounceDelay) && buttonState != lastButtonState) {
		lastButtonState = buttonState;
		debounceMillis = millis();
		buttonPushed = true;
	}
	return buttonPushed;
}

float getVoltage() {
	float batteryVoltage = analogRead(batteryPin) * 2.0 * 3.3 / 1024; //* 2.0 because a voltage divider sends only half the voltage to the batteryPin, * 3.3 (reference voltage), / # of steps
	return batteryVoltage;
}

void displayGas() {
	if ((millis() - lastSampleMillis) > sampleRate) {
		printFloat(sensor1.gasContent(), false, 0, 0);
		lcd.print("%");
		printFloat(sensor2.gasContent(), false, 0, 1);
		lcd.print("%");
		lcd.setCursor(6, 0);
		lcd.write(byte(0));
		lcd.setCursor(6, 1);
		lcd.write(byte(0));
		lastSampleMillis = millis();
		updateRightDisplay = true;
	}
}

void displayRight() {
	if (updateRightDisplay) {
		if (displayMode == 0) {
			if (!sensor1.isActive()) {
				lcd.setCursor(7, 0);
				lcd.print("         ");
			}
			else {
				lcd.setCursor(7, 0);
				lcd.write(byte(2));
				printFloat(sensor1.mv(), false, 8, 0);
				lcd.print(" mV");
			}
			if (!sensor2.isActive()) {
				lcd.setCursor(7, 1);
				lcd.print("         ");
			}
			else {
				lcd.setCursor(7, 1);
				lcd.write(byte(2));
				printFloat(sensor2.mv(), false, 8, 1);
				lcd.print(" mV");
			}
		}
		updateRightDisplay = false;
	}
}

void optionsMenu() {
	lastDisplayMillis = millis();
	currentSetting = 0;
	int lastMenuSelection = currentSetting;
	boolean exitOptionsMenu = false;
	clearRightScreen();

	lcd.setCursor(8, 0);
	lcd.print("Options");
	lcd.setCursor(10, 1);
	lcd.print("Menu");
	while (((millis() - lastDisplayMillis) < 1000 && currentSetting == 0)) {
		displayGas();
	}
	clearRightScreen();
	currentSetting = 0;
	while (!exitOptionsMenu) {
		displayGas();
		if (currentSetting > 1) {
			currentSetting = 0;
		}
		else if (currentSetting < 0) {
			currentSetting = 0;
		}
		if (currentSetting != lastMenuSelection) {
			clearRightScreen();
			lastMenuSelection = currentSetting;
		}

		switch (currentSetting) {
		case 0:
			lcd.setCursor(7, 0);
			lcd.print("Calibrate");
			if (buttonDetect(buttonPin)) {
				exitOptionsMenu = true;
				calibrate();
			}
			break;
		case 1:
			lcd.setCursor(7, 0);
			lcd.print("Exit");
			if (buttonDetect(buttonPin)) {
				clearRightScreen();
				exitOptionsMenu = true;
			}
			break;
		}
	}
}

void calibrate() {
	lcd.clear();
	lcd.setCursor(4, 0);
	lcd.print("Entering");
	lcd.setCursor(0, 1);
	lcd.print("Calibration Mode");
	delay(1750);
	lcd.clear();
	calibrateOxygen();
	/*
	while (digitalRead(buttonPin) == LOW) {
		}
		delay(250);
		lcd.print("Calibrate?");
		while (!buttonDetect(buttonPin)) {
			if (currentSetting > 2) {
				currentSetting = 0;
			}
			else if (currentSetting < 0) {
				currentSetting = 2;
			}
			lcd.setCursor(0, 1);
			lcd.write(byte(2));
			switch (currentSetting) {
			case 0:
				lcd.print("Oxygen Sensor");
				break;
			case 1:
				lcd.print("Helium Sensor");
				break;
			case 2:
				lcd.print("Both Sensors ");
				break;
			}
		}
		switch (currentSetting) {
		case 0:
			calibrateOxygen();
			break;
		case 1:
			calibrateHelium();
			break;
		case 2:
			calibrateOxygen();
			calibrateHelium();
			break;
		}
		*/
}


void calibrateOxygen() {
	float calibrationPoint;
	lcd.clear();
	lcd.print(" O2 Calibration");
	delay(1000);
	lcd.clear();
	lcd.print("Calibration FO2:");
	currentSetting = 209;

	while (!buttonDetect(buttonPin)) {
		if (currentSetting > 1000) {
			currentSetting = 1000;
		}
		else if (currentSetting < 0) {
			currentSetting = 0;
		}
		calibrationPoint = (float)currentSetting / 10.0;
		printFloat(calibrationPoint, true, 0, 1);
		lcd.print("% Oxygen");
	}
	lcd.clear();
	lcd.setCursor(7, 0);
	lcd.write(byte(0));
	lcd.print("Click to");
	lcd.setCursor(7, 1);
	lcd.write(byte(0));
	lcd.print("confirm");

	do {
		if (sensor1.mv() <= 0.0) {
			lcd.setCursor(0, 0);
			lcd.print("    ");
		}
		else {
			printFloat(sensor1.mv(), false, 0, 0);
		}
		lcd.print("mV");

	} while (!buttonDetect(buttonPin));
	calibrationPoint = calibrationPoint / 100.0;

	if (sensor1.isConnected() && sensor1.validateCalibration(calibrationPoint)) {
		sensor1.saveCalibration(sensor1.mv() / calibrationPoint);
	}

	else {
		lcd.clear();
		lcd.print("Bad Calibration");
		lcd.setCursor(0, 1);
		lcd.print("Data");
		delay(3000);
		calibrateOxygen();
	}
	lcd.clear();
	lcd.print("Calibration");
	lcd.setCursor(0, 1);
	lcd.print("Saved");

	delay(1500);
	lcd.clear();
	/*
		if (!sensor2.isCalibrated()) {
			calibrateHelium();
		}
	*/

}

/*
void calibrateHelium() {
	float mvOffset;
	lcd.clear();
	lcd.print(" HE Calibration");
	delay(1000);
	lcd.clear();
	lcd.print("mV in Air:");
	lcd.setCursor(0, 1);
	lcd.print("Click to Confirm");
	do {
		printFloat(sensor2.mv(), false, 10, 0);
		lcd.print("mV");
	} while (!buttonDetect(buttonPin));
	mvOffset = sensor2.mv();

	lcd.clear();
	lcd.print("mv in HE:");
	lcd.setCursor(0, 1);
	lcd.print("Click to Confirm");
	do {
		printFloat(sensor2.mv(), false, 10, 0);
		lcd.print("mV");
	} while (!buttonDetect(buttonPin));

	if (sensor2.validateCalibration(.99)) {
		sensor2.saveCalibration((sensor2.mv() - mvOffset)/1.0, mvOffset);
	}
	else {
		lcd.clear();
		lcd.print("Bad Calibration");
		lcd.setCursor(0, 1);
		lcd.print("Data");
		delay(3000);
		calibrateHelium();
	}
	lcd.clear();
	lcd.print("Calibration");
	lcd.setCursor(0, 1);
	lcd.print("Saved");

	delay(1500);
	lcd.clear();
	if (!sensor2.isCalibrated()) {
		calibrateOxygen();
	}
}
*/

//prints floats in a nicely formatted way so they don't jump around on the LCD screen
void printFloat(float floatToPrint, bool highlight, int column, int row) {
	String formattedValue = String(floatToPrint, 1);
	lcd.setCursor(column, row);
	if (highlight) {
		lcd.write(byte(2));
	}

	if (formattedValue.length() > 4) {
		formattedValue = formattedValue.substring(0, 3);
	}

	if (formattedValue.length() == 4) {
		lcd.print(formattedValue);
	}
	else {
		lcd.print(" ");
		lcd.print(formattedValue);
	}

	if (highlight) {
		lcd.setCursor(column + 5, row);
		lcd.write(byte(3));
	}
}

//prints ints in a nicely formatted way so they don't jump around on the LCD screen
void printInt(int intToPrint, bool highlight, int column, int row) {
	String formattedValue = String(intToPrint, DEC);

	lcd.setCursor(column, row);

	if (highlight) {
		lcd.write(byte(2));
	}

	if (formattedValue.length() == 2) {
		lcd.print(formattedValue);
	}
	else {
		lcd.print("0");
		lcd.print(formattedValue);
	}

	if (highlight) {
		lcd.setCursor(column + 3, row);
		lcd.write(byte(3));
	}

}

void clearRightScreen() {
	lcd.setCursor(7, 0);
	lcd.print("         ");
	lcd.setCursor(7, 1);
	lcd.print("         ");
}

//the first of two ISRs to detect pulses from the quadrature encoder
void aEncoderInterrupt() {
	aCurrentState = digitalRead(encoderPinA);
	bCurrentState = digitalRead(encoderPinB);

	if (aPreviousState && bPreviousState) {
		if (!aCurrentState && bCurrentState) {
			encoderTicks++;
		}
		if (aCurrentState && !bCurrentState) {
			encoderTicks--;
		}
	}
	else if (!aPreviousState && bPreviousState) {
		if (!aCurrentState && !bCurrentState) {
			encoderTicks++;
		}
		if (aCurrentState && bCurrentState) {
			encoderTicks--;
		}
	}
	else if (!aPreviousState && !bPreviousState) {
		if (aCurrentState && !bCurrentState) {
			encoderTicks++;
		}
		if (!aCurrentState && bCurrentState) {
			encoderTicks--;
		}
	}
	else if (aPreviousState && !bPreviousState) {
		if (aCurrentState && bCurrentState) {
			encoderTicks++;
		}
		if (!aCurrentState && !bCurrentState) {
			encoderTicks--;
		}
	}
	//the encoder I used has 4 pulses per detent, so I needed to add this if/else if statement to convert four pulses into a single adjustment point
	if (encoderTicks >= 4) {
		currentSetting++;
		encoderTicks = 0;
	}
	else if (encoderTicks <= -4) {
		currentSetting--;
		encoderTicks = 0;
	}
	aPreviousState = aCurrentState;
	bPreviousState = bCurrentState;
}

void bEncoderInterrupt() {
	aCurrentState = digitalRead(encoderPinA);
	bCurrentState = digitalRead(encoderPinB);
	if (aPreviousState && bPreviousState) {
		if (!aCurrentState && bCurrentState) {
			encoderTicks++;
		}
		if (aCurrentState && !bCurrentState) {
			encoderTicks--;
		}
	}
	else if (!aPreviousState && bPreviousState) {
		if (!aCurrentState && !bCurrentState) {
			encoderTicks++;
		}
		if (aCurrentState && bCurrentState) {
			encoderTicks--;
		}
	}
	else if (!aPreviousState && !bPreviousState) {
		if (aCurrentState && !bCurrentState) {
			encoderTicks++;
		}
		if (!aCurrentState && bCurrentState) {
			encoderTicks--;
		}
	}
	else if (aPreviousState && !bPreviousState) {
		if (aCurrentState && bCurrentState) {
			encoderTicks++;
		}
		if (!aCurrentState && !bCurrentState) {
			encoderTicks--;
		}
	}
	if (encoderTicks >= 4) {
		currentSetting++;
		encoderTicks = 0;
	}
	else if (encoderTicks <= -4) {
		currentSetting--;
		encoderTicks = 0;
	}
	aPreviousState = aCurrentState;
	bPreviousState = bCurrentState;
}
