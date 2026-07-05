#pragma once

// REQUIRED: set this before building/flashing.
// Keep the password private. Do not upload this file after adding your real password.
#define WIFI_SSID "YOUR_WIFI_NETWORK"
#define WIFI_PASSWORD "CHANGE_ME"

#define SERVER_URL "http://sparqm.com"
#define SERVER_HOST "sparqm.com"
#define SERVER_PORT 8001

#define I2S_BCLK 33
#define I2S_WS 32
#define I2S_SD 36
#define SAMPLE_RATE 16000

// 20 ms TCP frames for smoother continuous audio.
#define SAMPLES_PER_FRAME 320

// INMP441 is 24-bit data inside a 32-bit I2S word. V2.4 used >>16 and was too quiet.
// V2.7 uses >>12, then applies 95 percent gain.
#define MIC_SHIFT_BITS 12
#define GAIN_NUM 19
#define GAIN_DEN 20

// 0 = automatically choose the louder valid channel every frame.
// 1 = force RIGHT channel. 2 = force LEFT channel.
#define FORCE_AUDIO_CHANNEL 0
#define USB_SERIAL_PORT "/dev/cu.usbserial-0001"
