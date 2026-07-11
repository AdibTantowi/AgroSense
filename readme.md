<p align="center">
  <img src="assets/banner1.png" alt="AgroSense" width="100%">
</p>

<h1 align="center">AgroSense</h1>
<p align="center">
  Portable Soil Profiling System with On-Device KNN-Based Crop Recommendation
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-ESP32--S3-black">
  <img src="https://img.shields.io/badge/Accuracy-94.33%25-black">
  <img src="https://img.shields.io/badge/License-MIT-black">
</p>

<p align="center">
  <a href="https://agrosensepens.netlify.app/">Live Dashboard</a> ·
  <a href="#repository-structure">Structure</a> ·
  <a href="#results">Results</a> ·
  <a href="#getting-started">Getting Started</a>
</p>

---

## Description

AgroSense adalah alat ukur profil tanah berbasis ESP32-S3 yang membaca lima parameter tanah — Nitrogen, Fosfor, Kalium, pH, dan kelembapan — lalu langsung memberikan rekomendasi tanaman di lapangan lewat algoritma Weighted K-Nearest Neighbor yang berjalan on-device, tanpa perlu koneksi internet saat pengukuran. Setiap titik ukur dicatat lokasinya lewat GPS dan tersinkron otomatis ke dashboard web via Firebase.

Sistem ini dibangun untuk menjawab masalah konkret: petani sering menentukan tanaman berdasarkan kebiasaan, bukan kondisi tanah yang terukur. Dataset rekomendasi disusun dari 600 titik data lapangan yang diambil langsung di lahan binaan Dinas Ketahanan Pangan dan Pertanian Kota Surabaya, mencakup lima komoditas: padi, jagung, bawang merah, kangkung, dan kol. Nilai K optimal (K=15) ditentukan melalui eksperimen Elbow Method yang didokumentasikan pada notebook training.

Proyek ini dikerjakan sebagai Proyek Akhir D4 Teknik Elektronika, Politeknik Elektronika Negeri Surabaya.

---

## Features

- Akuisisi lima parameter tanah (NPK, pH, kelembapan) plus suhu dan GPS dalam satu perangkat
- Sensor multiplexing — dua sensor Modbus/RS485 berbagi satu jalur serial
- Multi-sampling averaging untuk mengurangi bias akibat sebaran pupuk tidak merata
- Klasifikasi KNN on-device dengan weighted voting dan reject option
- Geotagging otomatis pada setiap pengukuran
- Sinkronisasi real-time ke dashboard web via Firebase

---

## System Architecture

```mermaid
flowchart LR
    subgraph Device["ESP32-S3 Unit"]
        NPK[NPK Sensor] --> MUX[RS485 Multiplexer]
        PH[pH Sensor] --> MUX
        SM[Soil Moisture] --> MCU[ESP32-S3]
        TEMP[DS18B20] --> MCU
        GPS[NEO-6M GPS] --> MCU
        MUX --> MCU
        MCU --> KNN[Weighted KNN + Reject Option]
        KNN --> OLED[OLED Display]
    end
    MCU -- WiFi --> FB[(Firebase Realtime DB)]
    FB --> WEB[Web Dashboard]
```

---

## Repository Structure

```
agrosense/
├── File Program/         # Firmware ESP32-S3 (Arduino)
├── File Scematic/         # Skematik rangkaian & pin mapping
├── Model Training/         # Notebook Colab — penentuan nilai K (Elbow Method), evaluasi KNN
├── assets/                  # Banner, dokumentasi visual
└── README.md
```

> Dashboard web dikembangkan dan di-deploy terpisah — akses langsung di [agrosensepens.netlify.app](https://agrosensepens.netlify.app/).

---

## Hardware Components

| Component | Function | Interface |
|---|---|---|
| ESP32-S3 | Main microcontroller | — |
| SEN0605 (DFRobot) | NPK sensor | RS485/Modbus |
| VMS-3000 | Soil pH sensor | RS485/Modbus |
| Capacitive Soil Moisture V2.0 | Soil moisture | Analog |
| DS18B20 | Temperature | OneWire |
| NEO-6M | GPS module | UART |
| SSD1306 | OLED display | I2C |

---

## Results

| Metric | Value |
|---|---|
| KNN classification accuracy (K=15, 5-Fold CV) | 94.33% |
| Matthews Correlation Coefficient | 0.929 |
| ROC-AUC (multi-class) | 0.9947 |
| NPK sensor calibration (R²) | > 0.97 |
| pH sensor calibration (R²) | 0.9671 |
| GPS accuracy (open area) | ±2.5 m |
| Inference time per measurement | ~12 s |
| Training data | 600 points, 5 crop classes |

Rekomendasi yang dihasilkan sistem mencerminkan kemiripan profil tanah terhadap data latih dan berfungsi sebagai alat bantu keputusan, bukan pengganti keputusan agronomis final.

---

## Getting Started

### Firmware
```bash
# Buka folder "File Program" di Arduino IDE
# Pilih board: ESP32S3 Dev Module
# Sesuaikan kredensial WiFi & Firebase pada file konfigurasi
# Upload ke perangkat
```

### Schematic
Skematik rangkaian dan pemetaan pin tersedia di folder `File Scematic/`.

### Model Training
Proses penentuan nilai K optimal dan evaluasi algoritma KNN (cross-validation, confusion matrix, ROC-AUC) didokumentasikan pada notebook di folder `Model Training/`.

### Web Dashboard
Dashboard sudah live dan dapat diakses langsung di [agrosensepens.netlify.app](https://agrosensepens.netlify.app/) — tidak memerlukan instalasi lokal.

---

## Author

**M. Adib Tantowi Jauhari**
NRP 2122600001 — D4 Teknik Elektronika, Politeknik Elektronika Negeri Surabaya

Dosen Pembimbing: Ali Husein Alasiry, S.T., M.Eng · Ni'am Tamami, S.ST., M.T.

---

## License

Proyek ini dirilis di bawah lisensi [MIT](./LICENSE).
