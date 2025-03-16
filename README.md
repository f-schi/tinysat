# tinysat
TinySAT is a miniature model for remote sensing and earth observation to investiage change detection and forecasting.

## Overview

This project involves a satellite and ground segment system designed for capturing, processing, and visualizing sensor data. The satellite segment uses a **Seeed Studio XIAO ESP32-S3 Sense MCU** and a **VL53L8CX ToF (Time-of-Flight) sensor**. The data is transmitted via MQTT to the ground segment for validation, where it is further processed and made available for visualization through a web client.

---

## Satellite Segment

### Physical Setup:
- **Seeed Studio XIAO ESP32-S3 Sense MCU**
- **VL53L8CX ToF (Time-of-Flight) Sensor**

### Wiring Setup:

1. **Power Connections:**
   - Connect the **3V** pin from the XIAO ESP32-S3 to the **VCC** pin on the VL53L8CX ToF sensor.
   - Connect the **GND** pin from the XIAO ESP32-S3 to the **GND** pin on the VL53L8CX sensor.

2. **I2C Connections:**
   - Connect the **SDA** pin from the VL53L8CX sensor to **Pin 4** on the XIAO ESP32-S3.
   - Connect the **SCL** pin from the VL53L8CX sensor to **Pin 5** on the XIAO ESP32-S3.

Make sure all connections are secure and properly aligned.

### .INO Code:
- In the Arduino `.ino` code, add your **MQTT** and **Wi-Fi credentials** to ensure communication with the ground segment and network.

### What It Does:
- Captures an image and ToF sensor data.
- Publishes the captured data to MQTT:
  - **Image**: Encoded as Base64 for easy transmission.
  - **ToF Data**: Represented as a matrix of distances in mm (millimeters) captured from the VL53L8CX sensor.

---

## Ground Segment

### Physical Setup:
- **Sensebox MCU S2**

### .INO Code:
- Add your **MQTT** and **Wi-Fi credentials** in the code to allow the ground segment to communicate with the satellite.

### What It Does:
- Subscribes to MQTT sensor data publications from the satellite segment.
- Validates the received data by:
  - Checking for duplicate observations and ensuring the location and timestamps are synchronized.
  - Verifying all data points are complete.
  - Adding a validation timestamp for data integrity.
- Combines the validated data into a data packet and publishes it to MQTT for further use.

---

## Web Client for Raw Data Stream Visualization

### Installation & Setup:

1. Install **Node.js** from [https://nodejs.org/](https://nodejs.org/).
2. Install the necessary **MQTT** and **Socket.io** packages:
   ```bash
   npm install mqtt socket.io
   ```
3. Fill in your **MQTT credentials** in the `main.js` file.
4. Run the web client:
   ```bash
   node main.js
   ```

### What It Does:
- Subscribes to MQTT publications from the ground segment.
- Displays the following data on the web interface:
  - **Meta Data**: Includes information such as timestamps, sensor details, etc.
  - **Raw Image**: Displays the image captured from the satellite.
  - **3D Plot**: Visualizes the depth data from the ToF sensor in a 3D plot.

To view the web client, open [http://localhost:8000](http://localhost:8000) in your browser.
