# ESP32 MESH NETWORK

## Overview

This project implements a **mesh network using ESP32 devices** with integration to an **MQTT-based IoT system**.

Each ESP32 node operates as an independent unit in the mesh network, capable of:

* Reading sensor data (DHT11)
* Communicating with other nodes through mesh topology
* Sending data to an MQTT broker via a coordinator node

One node acts as a **coordinator (gateway)**, responsible for connecting the mesh network to the external MQTT cloud.

---

## Objectives

* Build a mesh network using ESP32 devices
* Implement multi-node communication without traditional infrastructure
* Integrate MQTT protocol for cloud communication
* Enable remote monitoring of sensor data

---

## System Architecture

The system consists of three main layers:

### 1. Sensor Nodes

* ESP32 + DHT11 sensor
* Collect temperature and humidity data
* Send data through mesh network

### 2. Mesh Network

* Nodes communicate using ESP32 mesh protocol
* Multi-hop communication between nodes
* Self-organizing and scalable topology

### 3. Coordinator Node

* Acts as gateway between mesh and MQTT broker
* Receives data from mesh nodes
* Publishes data to MQTT cloud

---

## System Workflow

1. Sensor node reads temperature and humidity from DHT11
2. Data is transmitted through the mesh network
3. Coordinator node receives aggregated data
4. Coordinator publishes data to MQTT broker
5. External clients can monitor data in real time

---

## Hardware Components

* ESP32 development boards
* DHT11 temperature and humidity sensors

---

## Technologies Used

### Networking

* ESP32 Mesh (Wi-Fi based mesh networking)
* Multi-hop communication

### Communication Protocol

* MQTT protocol
* Publish/Subscribe model

### Software

* PlatformIO, ESP-IDF
* C/C++ programming

---

## Features

* Mesh-based communication without router dependency
* Real-time sensor data acquisition
* MQTT cloud integration
* Scalable IoT architecture
* Distributed data collection

---

## Setup and Run

### 1. Environment Setup

* Install Arduino IDE or ESP-IDF
* Install ESP32 board support

### 2. Configure System

* Set Wi-Fi credentials
* Configure MQTT broker address
* Define node roles (sensor / coordinator)

### 3. Upload Firmware

* Flash sensor node code to ESP32 nodes
* Flash coordinator code to gateway node

### 4. Run System

* Power all ESP32 devices
* Mesh network forms automatically
* Sensor data is transmitted to MQTT broker

---

## Use Cases

* Smart home monitoring
* Environmental sensing
* Distributed IoT systems
* Industrial monitoring

---

## Limitations

* Latency increases with number of hops
* DHT11 has limited accuracy and sampling rate
* Network performance depends on topology and signal quality

---

## Future Work

* Replace DHT11 with higher-precision sensors
* Add device control via MQTT
* Implement OTA firmware update
* Improve network reliability and routing
