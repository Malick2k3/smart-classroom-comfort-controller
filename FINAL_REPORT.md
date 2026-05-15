# Smart Classroom Comfort Controller Final Report

## Group Members

- El Hadji Malick Niang
- Moustapha Lo
- Amadou Hanne
- Fallou Ndour

## Introduction

For our IoT final project, we worked on **Project 1: Smart Classroom Comfort Controller**. The main goal of the project was to build a system that can monitor classroom conditions and react automatically when the room becomes uncomfortable. In our prototype, we focused on temperature, humidity, occupancy, and fan control.

The idea behind the project is practical. In a normal classroom, devices like fans are often controlled manually. That can lead to wasted energy, especially when the room is empty or when the temperature is still acceptable. Our system tries to solve that by using sensors and simple automation rules.

## System Design

Our project follows the four main IoT layers.

### 1. Perception Layer

This is the physical layer, where the system interacts with the environment. We used:

- `ESP32` as the main controller
- `DHT11` for temperature and humidity
- `PIR sensor` for occupancy detection
- `Potentiometer` to set the threshold
- `OLED display` for local real-time feedback
- `Green, yellow, and red LEDs` for comfort indication
- `Relay module` for fan control output

### 2. Network Layer

The ESP32 connects to Wi-Fi and communicates using MQTT. MQTT was chosen because it is lightweight and well adapted to IoT communication. In the final version, we used the teacher's shared MQTT broker and protected our project data with a unique topic prefix for our group.

### 3. Processing Layer

We used a Python backend that subscribes to MQTT messages, stores the received data in an SQLite database, computes averages, and checks for high-temperature alert situations. This part handles logging and data management.

### 4. Application Layer

We created a Flask dashboard to visualize the live system state and recent saved readings. It shows temperature, humidity, occupancy, fan state, threshold, recent history, and alert information.

## System Logic

The system is based on a simple control rule:

- if the room is occupied and the temperature is above the threshold, the fan turns on
- if the room is occupied and the temperature is below the threshold, the fan stays off
- if the room becomes empty, the fan is allowed to keep running for a short time
- after the vacancy timeout, the fan turns off automatically

We also implemented comfort indication:

- `green` for comfortable
- `yellow` for warm
- `red` for hot

This makes the system easier to understand both on the hardware side and on the dashboard side.

## Implementation

The firmware on the ESP32 was developed using Arduino C++. We used a `millis()`-based non-blocking structure so the board could read sensors, refresh the OLED, read the potentiometer, update logic, and publish telemetry without using long delays.

The ESP32 publishes sensor data using MQTT in JSON format. The telemetry includes:

- temperature
- humidity
- occupancy
- fan state
- comfort state
- threshold

On the computer side, we used:

- `Python` for the subscriber and backend logic
- `SQLite` for data storage
- `Flask` for the dashboard

During development, we first used a local Mosquitto broker because it was easier to test and debug. In the final version, we switched to the teacher's shared MQTT broker using the provided IP address and credentials. We also added a unique topic prefix for our group so our messages would not mix with the traffic of other student groups.

So the project forms a complete chain from sensing to communication to storage to visualization. On top of that, the Python side computes averages and also checks if the temperature stays above the selected threshold for more than five minutes. When that happens, it records an alert in SQLite and the dashboard shows it.

## Challenges Faced

We faced several challenges while building the project.

The first challenge was hardware wiring and identification. At the beginning, we had to make sure we were using the right components from the kit and connecting them correctly. We solved this by wiring the system step by step instead of connecting everything at once.

The second challenge was sensor behavior. At first, the PIR and DHT11 did not seem responsive enough. We improved the firmware timing, added PIR warm-up handling, and stabilized occupancy detection.

Another challenge was the MQTT connection. The ESP32 first looked like it had a Wi-Fi problem, but after checking the Serial Monitor, we discovered that Wi-Fi was connected and the actual issue was with MQTT communication. During development, we fixed this by working with a local broker first. Later, we moved carefully to the teacher's shared broker and isolated our topics with a dedicated group prefix.

We also had some dashboard-side problems. Some updates were not appearing correctly at first because of stale processes and database schema changes. We corrected the backend, added database migration handling, and improved the dashboard so it could display more meaningful data.

Finally, we realized that real room conditions are not always ideal during a presentation. To solve that, we added prepared demo modes so we could clearly show empty, comfortable, warm, and hot scenarios.

We also had a practical issue when trying to add the real fan motor. Since small DC motors can generate electrical noise, we first decided to add a diode across the motor for protection. That was meant to reduce the back-kick effect when the motor switched off. But during testing, after adding the diode, the setup became unstable and one DHT11 sensor stopped reading correctly. We replaced the DHT11 and the system started working again, so we decided not to risk adding the diode back before the final demonstration.

## Results

At the end of the project, the system was able to:

- read temperature and humidity
- detect occupancy
- set the threshold with a potentiometer
- display live status on the OLED
- indicate comfort level with LEDs
- control a relay output for the fan logic
- publish telemetry through MQTT
- store readings in SQLite
- compute averages and high-temperature alerts
- display live, recent, and alert data in a Flask dashboard

This means the project successfully satisfies the main requirements of the Smart Classroom Comfort Controller.

## Conclusion

This project helped us understand that IoT is not just about sensors or code alone. A good IoT system needs proper hardware wiring, stable communication, correct data handling, and a clear user interface.

In conclusion, our Smart Classroom Comfort Controller shows how IoT can be used to improve classroom comfort and reduce unnecessary energy usage. Even though our project is a prototype, it demonstrates a complete working IoT pipeline from hardware sensing to application-level monitoring.
