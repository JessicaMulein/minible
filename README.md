# Welcome to the Mini BLE Firmware Repository!
<p align="center">
  <img src="https://github.com/mooltipass/minible/raw/master/_readme_assets/minible_front.jpg" alt="Mooltipass Mini BLE"/>
</p>
Here you will find the source code running on the Mooltipass Mini BLE auxiliary and main microcontrollers.  

## What is the Mooltipass Project?
The Mooltipass project is a complete ecosystem aimed at providing authentication solutions. It is composed of:  
- A <a href="https://github.com/mooltipass/minible_hw">physical device</a>, providing all security-related features  
- Multiple <a href="https://github.com/mooltipass/extension">browser extensions</a> (Chrome, Firefox, Edge, Opera) for easy credentials storage & recall  
- A <a href="https://github.com/mooltipass/moolticute">cross-plaform user interface</a>, for easy management of the physical device features and database  
- A <a href="https://github.com/mooltipass/moolticute">cross-platform software daemon</a>, serving as an interface between device and software clients  
- An <a href="https://github.com/raoulh/mc-agent">SSH agent</a>, providing password-less SSH authentication using a Mooltipass device  
- A <a href="https://github.com/oSquat/mooltipy">python library</a> to recall credentials stored on the Mooltipass
- A <a href="https://github.com/raoulh/mc-cli">command line tool written in go</a> to interact with the Mooltipass device

## The Mooltipass Devices
<p align="center">
  <img src="https://github.com/mooltipass/minible/raw/master/_readme_assets/ble_vaults_cards.png" alt="Mooltipass Mini BLE"/>
</p>
All Mooltipass devices (<a href="https://github.com/limpkin/mooltipass/tree/master/kicad/standard">Mooltipass Standard</a>, <a href="https://github.com/limpkin/mooltipass/tree/master/kicad/mini">Mooltipass Mini</a>, <a href="https://github.com/mooltipass/minible">Mooltipass Mini BLE</a>) are based on the same principle: each device contains one (or more) user database(s) AES-256 encrypted with a key stored on a PIN-locked smartcard. This not only allows multiple users to share one device but also one user to use multiple devices, as the user database can be safely exported and the smartcard securely cloned.  

## The Mini BLE Architecture
<p align="center">
  <img src="https://github.com/mooltipass/minible/raw/master/_readme_assets/ble_architecture.png" alt="Mooltipass Mini BLE"/>
</p>
The firmwares in this repository are made for the device architecture shown above.     

The Mooltipass Mini BLE is composed of <b>two microcontrollers</b>: an <a href="https://github.com/mooltipass/minible/tree/master/source_code/aux_mcu">auxiliary one</a> dedicated to <b>USB and Bluetooth communications</b> and a <b>secure microcontroller</b> dedicated to running all security features. You may read about the rationale behind this choice <a href="https://mooltipass.github.io/minible/highlevel_overview">here</a>. The device microcontrollers communicate with each other using a <a href="https://mooltipass.github.io/minible/aux_main_mcu_protocol">high speed serial link</a>.

## Auxiliary MCU Firmware Features
The auxiliary microcontroller mostly provides communication features.   

### USB Communications
Our <a href="https://github.com/mooltipass/minible/tree/master/source_code/aux_mcu/src/USB">USB interface</a> provides <b>three</b> communication channels:     
- A keyboard HID interface for the Mooltipass Mini BLE to simulate keypresses
- A <a href="https://mooltipass.github.io/minible/usb_hid_protocol">custom HID interface</a> for <a href="https://mooltipass.github.io/minible/protocol">mooltipass communications</a>
- A FIDO2 HID interface to support password-less authentication

### BLE Communications
The Mooltipass Mini BLE uses the ATBTLC1000 to provide Bluetooth Low Energy connectivity. It currently provides <b>two</b> communication channels:   
- A keyboard HID interface for the Mooltipass Mini BLE to simulate keypresses
- A <a href="https://mooltipass.github.io/minible/usb_hid_protocol">custom HID interface</a> for communications with computers

### BLE Communications: Help Needed!
It is in our plans to provide two addition communication channels for Bluetooth:
- One for FIDO2 features
- One to provide communcation with a mobile app providing autofill services for <a href="https://developer.android.com/guide/topics/text/autofill-services">Android</a> and <a href="https://developer.apple.com/documentation/security/password_autofill">iOS</a>

## Main MCU Firmware Features
### Graphical User Interface
Our user interface is the fruit of several years of work. It includes:  
- <a href="https://mooltipass.github.io/minible/bundle">Update files, language strings, font files, bitmap files and keyboard files<a> bundle storage in external flash   
- A read-only file system for parsing that bundle file: <a href="https://github.com/mooltipass/minible/blob/master/source_code/main_mcu/src/FILESYSTEM/custom_fs.c">custom_fs.c</a>
- 256x64x4bpp SH1122-based OLED screen support, with internal frame buffer: <a href="https://github.com/mooltipass/minible/blob/master/source_code/main_mcu/src/OLED/sh1122.c">sh1122.c</a>
- Run-length graphical files decoding: <a href="https://github.com/mooltipass/minible/blob/master/source_code/main_mcu/src/FILESYSTEM/custom_bitstream.c">custom_bitsteam.c</a>
- Unicode Basic Multilingual Plane support
- Multiple languages support on device
- Language-based fonts support
  
### User Database
Our database model is documented <a href="https://mooltipass.github.io/minible/database_model">here</a>. Its main characteristics are:  
- Multiple doubly linked list-based credential and file storage
- Parent (services) - Child (credentials) structure
- Credential categories support
- Credential favorites support
- Webauthn custom credential type  

At the time of writing, the Mini BLE can handle logins & passwords up to 64 unicode characters long.

### Manual Credential Typing
Mooltipass devices can simulate key presses in order to type logins & passwords onto the computer they're connected to. As the "byte sent on the wire" to "actual character typed on the computer" conversion is handled by the computer itself, that meant the Mooltipass devices need to handle multiple keyboard layouts.  
For the Mooltipass Mini BLE, we are <a href="https://github.com/mooltipass/minible/tree/master/scripts/keyboards">parsing the CLDR</a> to make sure we can type text <a href="https://mooltipass.github.io/minible/unicode_layouts">on any device</a>.

### Authentication Features
The Mooltipass team selected <a href="https://bearssl.org/">BearSSL</a> for cryptographic routines. The remaining code was made from the ground up.
The Mooltipass Mini BLE includes the following authentication features:
- Standard login/password authentication, by key presses or with its own communication channel
- Webauthn / FIDO2 password-less authentication 
- TOTP second factor authentication

### Note Worthy Items
Creating these firmwares from the ground up allowed us to create a smooth user-experience, tailored to privacy-minded indviduals. Here are some things that are worth mentioning:  
- All transfers to peripherals, displays and MCUs are <a href="https://github.com/mooltipass/minible/blob/master/source_code/main_mcu/src/DMA/dma.c#L29">DMA-based</a>
- A custom NiMH charging algorithm was implemented
- An accelerometer is used as a source of entropy
- All source code is doxygen-style documented

## Device Emulator
<p align="center">
  <img src="https://github.com/mooltipass/minible/raw/master/_readme_assets/emulator_working_ubuntu.PNG" alt="Mooltipass Mini BLE"/>
</p>
Device emulators are available for <a href="https://github.com/mooltipass/minible/releases">Windows</a> and <a href="https://launchpad.net/~mooltipass/+archive/ubuntu/minible-beta">Ubuntu</a>. Together with <a href="https://github.com/mooltipass/moolticute">Moolticute</a>, you will be able to test our complete ecosystem without a physical device.

## Contributing to the Mooltipass Firmware
<p align="center">
  <img src="https://github.com/mooltipass/minible/raw/master/_readme_assets/dev_board.PNG" alt="Mooltipass Mini BLE"/>
</p>
The Mooltipass team welcomes contributions from open source enthusiasts! Feel free to have a look at <a href="https://github.com/mooltipass/minible/issues?q=is%3Aissue+is%3Aopen+label%3A%22feature+request%22">our feature requests list</a> to see what you could contribute on. Depending on the task, we could ship you one of our developpement boards (shown above), or you could also develop <a href="https://mooltipass.github.io/minible/developing_using_emulator">using our device emulator</a>. Do not forget to review our contributing guidelines!

## Adding a New Language to the Mini BLE
The following languages are currently supported:
- English
- Catalan
- German
- French
- Italian
- Croatian
- Dutch
- Portuguese
- Spanish
- Slovene
- Finnish   

If your language is not listed, **get in touch with us** to then be able to follow <a href="https://mooltipass.github.io/minible/translators_guidelines">these instructions</a>.

## [](#header-2)Keeping in Touch
Get in touch with the development team and other Mooltipass enthuasiasts on our **Mooltipass IRC channel**: **#mooltipass** on **irc.freenode.net**

## [](#header-2)Licenses
<p align="center">
  <img src="https://github.com/mooltipass/minible/raw/master/_readme_assets/licenses.PNG" alt="Mooltipass Mini BLE"/>
</p>
