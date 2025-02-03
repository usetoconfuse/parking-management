# Arduino Parking Management System

*This program was designed for an Arduino UNO Rev3 equipped with an Adafruit RGB LCD Shield, which provides a 16x2 LCD display and several buttons.*

This C++ program manages cars in a car park. It tracks vehicles as they arrive, pay and leave different locations.

### Vehicles

Each vehicle has the following attributes:
- A character specifying the vehicle type from 5 types: [C]ar, [M]otorcycle, [V]an, [L]orry, [B]us
- A registration number of the form XX00XXX (all letters are capitals)
- A location string whose characters can be letters, numbers or periods, maximum length 11 characters.
- A status which is either PD (Paid) or NPD(Not Paid). Arriving vehicles have NPD by default.
- An entry timestamp of the form HHMM using 24-hour time, e.g 1426
- An exit timestamp of the same form, only if the vehicle has PD status

### Operation

*At any point in operation, holding the select button will display the amount of free SRAM.*

The program begins in the synchronisation phase. The screen will be blank and the board will send 'Q' characters over the Serial interface once per second.
After receiving an 'X' character, synchronisation completes and the main phase begins.

The program saves the vehicle list to EEPROM after each command, so if prior data exists it will load this. The screen will be blank if the list is empty.
The up and down buttons are used to navigate the list. The left and right buttons filter the list by PD and NPD vehicles respectively. Pressing the opposite button resets the filter.

During the main phase, the board receives commands to edit the current list of vehicles.
Commands are of the form X-REGNUMB-Y, where X is the command type, REGNUMB is the registration of the vehicle and Y is command-specific information.
The command types are:
- A : Add a vehicle with the given regnum, type, and location, e.g. A-GT73BNS-V-BigHill
- S : Change the vehicle's payment status to the given status, e.g. S-OY82MJG-PD
- T : Change the vehicle's type if it has paid, e.g. T-RS67LKV-L
- L : Change the vehicle's location if it has paid, e.g. L-ND82PAO-RoadStreet
- R : Remove the vehicle from the list if it has paid, e.g. R-OD94NBG

After a successful command, the board prints "DONE!" to the serial interface.
The board will also print error messages for invalid inputs and debug messages indicating program status.
