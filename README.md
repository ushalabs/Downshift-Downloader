<p align="center">
  <img src="docs/Downshift_banner.png" alt="Downshift Banner" width="100%" />
</p>

<h1 align="center">Downshift</h1>

<p align="center">
  <strong>An all-in-one download manager for Windows.</strong><br />
  Fast file downloads, video downloads, torrent support, browser integration, and a modern native desktop experience.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-Windows%2010%20%2F%2011-0078D4?logo=windows&logoColor=white" />
  <img src="https://img.shields.io/badge/version-1.0.0-EC1680" />
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B&logoColor=white" />
  <img src="https://img.shields.io/badge/Qt-6-41CD52?logo=qt&logoColor=white" />
  <img src="https://img.shields.io/badge/license-MIT-blue" />
</p>

<p align="center">
  <strong>Microsoft Store Soon</strong> • <strong>Android Port Soon</strong>
</p>

---

## About Downshift

**Downshift** is a modern native Windows download manager built to handle different kinds of downloads in one place.

It supports:

- **Regular file downloads**
- **Video downloads**
- **Torrent downloads**
- **Browser-assisted capture**
- **Pause / Resume**
- **Queue and speed management**
- **A modern desktop UI with light and dark theme support**

The goal of Downshift is to provide a clean, fast, and reliable download experience without the outdated feel of older download managers.

---

## Main Interface

<p align="center">
  <img src="docs/Downshift%201.png" alt="Downshift Main Interface" width="100%" />
</p>

The main dashboard gives a quick overview of:

- total downloads
- active transfers
- total speed
- downloaded data
- in-progress, completed, and attention-needed items
- torrent downloads
- download controls and search

---

## Features

### All-in-one downloading
Downshift combines multiple download workflows into a single desktop application:

- **HTTP / HTTPS downloads**
- **Video downloads from supported sources**
- **Torrent and magnet downloads**
- **Browser integration**
- **Unified download history**

### Modern desktop UI
Designed with a cleaner and more modern interface than traditional download managers:

- spacious layout
- light and dark theme support
- searchable download list
- categorized sidebar
- live status cards
- detailed download information
- native Windows feel

### Download management
Downshift includes the core controls expected from a serious downloader:

- pause / resume
- cancel
- queueing
- connection controls
- speed visibility
- organized history
- status-based filtering
- settings for behavior and appearance

### Torrent support
Torrent downloads are built directly into the application so users can manage normal downloads and torrents in one place.

### Browser integration
Downshift is designed to work alongside browser download flows to make grabbing supported downloads easier.

---

## Tech Stack

Downshift is built with:

- **C++20**
- **Qt 6**
- **libcurl**
- **SQLite**
- **Rasterbar libtorrent**
- **CMake**
- **MSVC / Visual Studio**
- **Windows-native packaging**

This project is focused on native desktop performance, modern UI, and real-world usability.

---

## Settings

<p align="center">
  <img src="docs/Downshift%202.png" alt="Downshift Settings" width="100%" />
</p>

The settings screen allows configuration for things like:

- download folder
- simultaneous downloads
- downloads per site
- connections per site
- connections per download
- total speed limit
- appearance
- integration-related behavior

It also carries the **UshaLabs** branding as part of the project identity.

---

## Project Status

### Windows
The Windows version is the primary version of Downshift and is currently the main focus.

### Microsoft Store
**Microsoft Store release is planned soon.**

### Android
An **Android port is planned soon** to extend Downshift beyond desktop and bring the same all-in-one idea to mobile.

---

## Why this project matters

Downshift is not just a UI showcase — it is a real software engineering project built around:

- native desktop development
- networking
- download workflows
- torrent integration
- file handling
- UI/UX design
- packaging and deployment

It is being developed as both a usable application and a strong portfolio project.

---

## Repository Structure

```text
Downshift-Downloader/
├── assets/
├── docs/
├── extension/
├── packaging/
├── scripts/
├── src/
├── tests/
├── CMakeLists.txt
└── README.md
