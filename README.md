<p align="center">
  <img src="screenshot.png" alt="HomeStack Interface" width="608">
</p>

# 🏠 HomeStack

**HomeStack** is a lightweight, portable management interface for orchestrating local server infrastructure on Windows. It provides a native control plane for the **AMP stack (Apache, MariaDB, PHP)**, pre-integrated with **Composer** and **phpMyAdmin** to deliver a high-performance local development environment.

## 🌟 Key Features

- **Native Windows Control Plane**: A lightweight interface designed for low overhead and maximum stability.
- **Zero-Config AMP Stack**: Pre-configured **Apache**, **MariaDB**, and **PHP** optimized for out-of-the-box compatibility.
- **Portable by Design**: Run your environment from a USB drive or synced cloud folder—no registry changes or complex installers.
- **Built-in Tooling**: Integrated **Composer** for dependency management and **phpMyAdmin** for effortless database administration.
- **SSL Ready**: Simple batch scripts to generate and trust local SSL certificates for HTTPS development.

## 🚀 Quick Start

## 🛠 Requirements

Before building or running HomeStack, ensure you have the following tools configured:

- **C/C++ Compiler**: [winlibs-w64ucrt](https://winlibs.com/) is recommended for native Windows compatibility.
- **Make**: Use `mingw32-make.exe`.
  - _Tip: Rename `mingw32-make.exe` to `make.exe` in your `../GCCx64/bin` folder for easier command-line usage._
- **Resource Editor**: [ResEdit](https://resedit.apponic.com/) (or a reliable mirror) for managing application icons and metadata.
- **Code Editor**: [VS Code](https://code.visualstudio.com) or your preferred IDE.

### 1. Installation

1.  **Download** the latest release from the [Releases](https://github.com) page.
2.  **Extract** the ZIP archive to your preferred directory (e.g., `C:\HomeStack`).
3.  **Run** `HomeStack.exe` to launch the control panel.

### 2. Service Management

- Launch the control plane and click **Start** next to Apache and MariaDB.
- The status indicators will turn **green** once services are live.

### 3. Project Setup

- On the first launch, a dialog will prompt you to select your **Root Web Directory**.
- Place your project folders within this directory.
- Access projects via `http://localhost/your-project-name`.

### 4. Database Access

- **Interface**: Click the **phpMyAdmin** button or visit `http://localhost/phpmyadmin`.
- **Credentials**:
  - **User**: _(None by default)_
  - **Password**: _(None by default)_

### 5. HTTPS & SSL

To enable secure browsing:

1.  Navigate to `config/`.
2.  Run `create_cert.bat`.
3.  **Install the certificate** to your Trusted Root Certification Authorities (Chrome/Edge). For **Firefox**, manually import the certificate via Firefox Settings.

## 🛠 Dependency Management

HomeStack automatically adds **Composer** to your environment path while the interface is active. Simply open your terminal and run standard commands:

```bash
composer install
```
