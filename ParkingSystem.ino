#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>
#include <TimeLib.h>
#include <MemoryFree.h>
#include <TimerOne.h>
#include <avr/eeprom.h>


// Extensions: UDCHARS, FREERAM, HCI, EEPROM, SCROLL


Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();


byte upArrowArr[8] = {
	0b00100,
	0b01110,
	0b10101,
	0b00100,
	0b00100,
	0b00100,
	0b00000,
	0b00000
};

byte downArrowArr[8] = {
	0b00000,
	0b00100,
	0b00100,
	0b00100,
	0b10101,
	0b01110,
	0b00100,
	0b00000
};



// State enumeration does not contain the SYNC state
// since this is contained in the setup function.
enum states {
  DISPLAYING,
  ADDING_VEHICLE,
  REMOVING_VEHICLE,
  CHANGING_PAY_STATUS,
  CHANGING_TYPE,
  CHANGING_LOCATION
};

// Enumeration for the different sub-arrays that
// could be displayed, for HCI.
enum currentDisplay {
  MAIN,
  UNPAID,
  PAID
};


#define exitstate state = DISPLAYING; break


const char commands[6] PROGMEM = {'A', 'R', 'S', 'T', 'L'};
const char types[6] PROGMEM = {'C', 'M', 'V', 'L', 'B'};

// Constant to hold vehicle array length and vehicle sub array length.
// Each vehicle occupies 33 bytes of memory, so increasing vArrLen by
// 1 reserves an extra 66 bytes in SRAM.

// I have left the length as 15 since it leaves
// 200-300 bytes of memory free, which should be enough.
const int vArrLen = 15;
// Program uses 611 bytes of memory excluding reserved array space.

// Global variable used by interrupt to display free SRAM in real time.
volatile int freeMem;


class Vehicle {
  private:
    // Function to assign a given char array a timestamp.
    // Used to assign entryTime on vehicle creation.
    // And exitTime when a vehicle pays.
    void CreateTime(char timeVar[5]) {
      int h = hour();
      int m = minute();

      String hs;
      String ms;

      // Add leading zeroes if necessary.
      if (h < 10) {
        hs = '0' + String(h);
      }
      else {
        hs = String(h);
      }

      if (m < 10) {
        ms= '0' + String(m);
      }
      else {
        ms = String(m);
      }

      char tempVar[5];
      (hs + ms).toCharArray(tempVar, 5);
      strcpy(timeVar, tempVar);
    }

  public:
    char regNum[8];
    char entryTime[5];
    char exitTime[5];
    char location[12];
    char type;
    bool hasPaid;
    bool fromStorage;

    void CreateEntryTime() {
      CreateTime(entryTime);
    }

    void CreateExitTime() {
      CreateTime(exitTime);
    }

    void ClearExitTime() {
      strcpy(exitTime, "    ");
    }
    
    // Empty constructor.
    // This only exists to allow the creation of an empty array to hold vehicles.
    // The type is set to NULL as this is used throughout the program to represent
    // an empty entry in the array of vehicles.
    Vehicle() {
      type = NULL;
      fromStorage = false;
    }

    // Populated constructor.
    // All vehicles created through commands use this constructor.
    Vehicle(char regStr[], char typeChar, char locStr[]) {
      strcpy(regNum, regStr);
      strcpy(location, locStr);
      type = typeChar;
      hasPaid = false;
      fromStorage = false;
      CreateEntryTime();
      ClearExitTime();
    }
};


// Interrupt handler for free SRAM.
void UpdateFreeMem() {
  freeMem = freeMemory();
}



// EEPROM FUNCTIONS

// Function to store a given vehicle in EEPROM, can also
// choose to store a timestamp.
void StoreVehicle(Vehicle v, int index, bool storeTime) {

  // Find correct section of EEPROM for the corresponding vehicle.
  // The first byte is used as a flag to check if data has previously been stored.
  // The next 4 bytes are allocated to the most recent timestamp.
  // After this, each vehicle is allocated 33 bytes.
  int romIndex = index * 33 + 5;
  
  // Write the vehicle's information to EEPROM.
  eeprom_write_block(v.regNum, romIndex, (size_t)8);
  romIndex += 8;
  eeprom_write_block(v.entryTime, romIndex, (size_t)5);
  romIndex += 5;
  eeprom_write_block(v.exitTime, romIndex, (size_t)5);
  romIndex += 5;
  eeprom_write_block(v.location, romIndex, (size_t)12);
  romIndex += 12;
  eeprom_write_byte((uint16_t)romIndex, (uint16_t)v.type);
  romIndex += 1;
  eeprom_write_byte((uint16_t)romIndex, (uint16_t)v.hasPaid);
  romIndex += 1;
  eeprom_write_byte((uint16_t)romIndex, true);

  if(storeTime) {
    time_t time = now();
    eeprom_write_dword((uint32_t)1, (uint32_t)time);
  }
}

// Iterate over vehicle array and store all vehicles in EEPROM.
void StoreArray(Vehicle vArr[]) {
  for (int i = 0; i < vArrLen; i++) {
    StoreVehicle(vArr[i], i, false);
  }

  // Store timestamp.
  time_t time = now();
  eeprom_write_dword((uint32_t)1, (uint32_t)time);

  // Set flag to indicate data has been stored.
  eeprom_write_byte((uint8_t)0, (uint8_t)255);
}


// Retrieve stored timestamp and vehicle array data from EEPROM.
// Returns true if retrieved successfully, false if there was no data to read.
bool ReadData(Vehicle vArr[]) {

  // Don't read data if flag for previous storage is not set.
  int prevStored = eeprom_read_byte((uint8_t)0);
  if (prevStored != (uint8_t)255) {
    return false;
  }

  // Update system time to the time of the last operation.
  time_t time;
  time = eeprom_read_dword((uint32_t)1);
  setTime(time); 

  // Retrieve all stored vehicle array data.
  for (int index = 0; index < vArrLen; index++) {
    int romIndex = index * 33 + 5;

    // Check that the vehicle is present before trying to
    // read the rest of its information.
    romIndex += 30;
    vArr[index].type = eeprom_read_byte((uint16_t)romIndex);
    if (vArr[index].type == NULL) {
      return true;
    }
    romIndex -= 30;

    // Read vehicle information.
    eeprom_read_block(vArr[index].regNum, romIndex, (size_t)8);
    romIndex += 8;
    eeprom_read_block(vArr[index].entryTime, romIndex, (size_t)5);
    romIndex += 5;
    eeprom_read_block(vArr[index].exitTime, romIndex, (size_t)5);
    romIndex += 5;
    eeprom_read_block(vArr[index].location, romIndex, (size_t)12);
    romIndex += 13;
    vArr[index].hasPaid = eeprom_read_byte((uint16_t)romIndex);
    romIndex += 1;
    vArr[index].fromStorage = eeprom_read_byte((uint16_t)romIndex);
  }
  return true;
}

// END EEPROM FUNCTIONS



// DISPLAY FUNCTIONS

// This function checks if the select button has been held for over a second
// and updates the display accordingly.
// This feature has priority over all other display functions.
bool SelectHeld() {

  // Retrieve free memory
  noInterrupts();
  int freeMemCpy = freeMem;
  interrupts();

  uint16_t buttons = lcd.readButtons();

  bool selectPressed = false;
  static bool selectWasPressed = false;
  static bool selectHeld = false;
  static unsigned long selectTimer = 0;

  if (buttons & BUTTON_SELECT) {
    // If select was just pressed, start a timer
    if (!selectWasPressed) {
      selectTimer = millis(); 
    }

    selectPressed = true;
    selectWasPressed = true;
  }

  // If select was just released, reset the held flag.
  if (selectWasPressed && !selectPressed) {
    selectWasPressed = false;
    selectHeld = false;
    lcd.clear();
    return selectHeld;
  }

  // If select has been held for over a second,
  // Set the select held flag
  // and make the backlight purple and display
  // student id number and the current free SRAM.
  if (selectWasPressed && millis() >= selectTimer + 1000) {
    if (!selectHeld) {
      lcd.clear();
      lcd.setBacklight(5);
      lcd.setCursor(0, 0);
      lcd.print(F("F319706 "));
      lcd.print(freeMemCpy);
      lcd.print(F(" B"));
      selectHeld = true;
    }
    else {
      lcd.setCursor(8, 0);
      lcd.print(freeMemCpy);
    }
  }

  return selectHeld;
}


// This function handles all logic related to the display during the main phase.

// This function returns the index so that if the page has changed,
// the updated page index is preserved after the function is exited.
// Returns:
// -1 if filtering by unpaid status
// -2 if filtering by paid status
// -3 if resetting to main display from a sub display.
int UpdateDisplay(Vehicle vArr[], int index, int display) {

  bool empty = false;

  // Update the display if the current array is empty.
  // Display changes based on the current array.
  if (vArr[0].type == NULL) {

    if (SelectHeld()) {
      empty = true;
    }

    else if (display == 0) {
      lcd.clear();
      lcd.setBacklight(7);
    }
    
    else if (display == 1) {
      lcd.setBacklight(3);
      lcd.setCursor(0, 0);
      lcd.print(F("NO NPD VEHICLES "));
    }

    else if (display == 2) {
      lcd.setBacklight(2);
      lcd.setCursor(0, 0);
      lcd.print(F("NO PD VEHICLES  "));
    }

    lcd.setCursor(0, 1);
    lcd.print(F("                "));

    empty = true;
  }

  // If the current index points to an empty array entry
  // because it has just been removed, move the index
  // back until it is displaying a valid entry.
  else {
    while (vArr[index].type == NULL) {
      index--;
    }
  } 

  // Cycle page if up/down is pressed, and the display
  // isn't at the top/bottom of the list.
  // This only cycles when a button is released to prevent
  // extremely fast scrolling.
  uint16_t buttons = lcd.readButtons();

  bool upPressed = false;
  static bool upWasPressed = false;

  bool downPressed = false;
  static bool downWasPressed = false;

  bool leftPressed = false;
  static bool leftWasPressed = false;
  static bool displayNPD = false;

  bool rightPressed = false;
  static bool rightWasPressed = false;
  static bool displayPD = false;

  static bool pageChanged = false;

  if (buttons & BUTTON_UP) {
    upPressed = true;
    upWasPressed = true;
  }
  // Update when up is released.
  if (upWasPressed && !upPressed) {
    upWasPressed = false;
    if (index > 0) {
      index--;
      pageChanged = true;
    }
  }

  if (buttons & BUTTON_DOWN) {
    downPressed = true;
    downWasPressed = true;
  }
  // Update when down is released.
  if (downWasPressed && !downPressed) {
    downWasPressed = false;
    if (!(vArr[index+1].type == NULL || index == vArrLen - 1)) {
      index++;
      pageChanged = true;
    }
  }

  if (buttons & BUTTON_LEFT) {
    leftPressed = true;
    leftWasPressed = true;
  }
  // Update when left is released.
  if (leftWasPressed && !leftPressed) {
    leftWasPressed = false;
    if (displayPD) {
      displayPD = false;
      pageChanged = true;

      if (!SelectHeld()) {
        lcd.clear();
      }

      return -3;
    }
    else if (!displayNPD){
      displayNPD = true;
      pageChanged = true;

      if (!SelectHeld()) {
        lcd.clear();
      }

      return -1;
    }
  }

  if (buttons & BUTTON_RIGHT) {
    rightPressed = true;
    rightWasPressed = true;
  }
  // Update when right is released.
  if (rightWasPressed && !rightPressed) {
    rightWasPressed = false;
    
    if (displayNPD) {
      displayNPD = false;
      pageChanged = true;

      if (!SelectHeld()) {
        lcd.clear();
      }

      return -3;
    }
    else if (!displayPD) {
      displayPD = true;
      pageChanged = true;

      if (!SelectHeld()) {
        lcd.clear();
      }
      
      return -2;
    }
  }

  // Don't try to update the display if there's
  // nothing to display.
  if (empty) {
    return 0;
  }

  // Change displayed arrows if the current page is the bottom or top.
  char arrowUp;
  char arrowDown;

  if (index == 0) {
    arrowUp=' ';
  }
  else {
    arrowUp=(uint8_t)0;
  }

  // Check if the next array entry is empty or the current entry is
  // the last in the array.
  if (vArr[index+1].type == NULL || index == vArrLen - 1) {
    arrowDown = ' ';
  }
  else {
    arrowDown=(uint8_t)1;
  }

  // Do not change the display if select is currently being held.
  if (SelectHeld()) {
    return index;
  }

  // Change text to display according to vehicle's payment status.
  char * payText;
  if (vArr[index].hasPaid) {
    payText = " PD";
    lcd.setBacklight(2);
  }
  else {
    payText = "NPD";
    lcd.setBacklight(3);
  }


  // Character array used to display scrolling location.
  static char scrollLoc[12];

  // Stores the value of scrollLoc without scroll effect applied.
  // Used to check if the location has changed as a result of a
  // button press or a command.
  static char scrollLocNorm[12];

  static unsigned long scrollTimer = 0;
  static unsigned long dScrollTimer = 0;
  static int locLength = 0;
  
  // If location information or page has changed, check if
  // the new location is long enough to require scrolling.
  if (pageChanged || strcmp(scrollLocNorm, vArr[index].location) != 0) {
    lcd.clear();

    scrollTimer = millis();
    strlcpy(scrollLoc, vArr[index].location, 12);
    strlcpy(scrollLocNorm, vArr[index].location, 12);
    locLength = 0;

    while (scrollLoc[locLength] != NULL) {
      locLength++;
    }

    pageChanged = false;
  }

  // Set dScrollTimer to -1 to indicate that
  // the location is too short for scrolling.
  if (locLength < 8) {
    dScrollTimer = -1;
  }
  else {
    dScrollTimer = millis();
  }

  // Scroll every 0.5 seconds.
  if (dScrollTimer != -1) {

    dScrollTimer = millis();
    if (dScrollTimer >= scrollTimer + 500) {

      if (scrollLoc[7] == NULL) {
        strlcpy(scrollLoc, vArr[index].location, 12);
        scrollTimer = dScrollTimer;
      }

      else {
        for (int i = 0; scrollLoc[i] != NULL; i++) {
          scrollLoc[i] = scrollLoc[i+1];
          scrollTimer = dScrollTimer;
        }
      }
    }
  }

  

  // Write the new information to the screen.
  lcd.setCursor(0, 0);
  lcd.print(arrowUp);
  lcd.print(vArr[index].regNum);
  lcd.print(F(" "));
  lcd.print(scrollLoc);

  lcd.setCursor(0, 1);
  lcd.print(arrowDown);
  lcd.print(vArr[index].type);
  lcd.print(F(" "));
  lcd.print(payText);
  lcd.print(F(" "));
  lcd.print(vArr[index].entryTime);
  lcd.print(F(" "));
  lcd.print(vArr[index].exitTime);

  return index;
}

// Copy all entries in the main array with a given status into a sub array.
void FilterVehicleArray(Vehicle vArrSub[], Vehicle vArr[], bool status) {

  int i = 0;
  Vehicle temp;

  for (int j = 0; j < vArrLen; j++) {
    vArrSub[j] = temp;
    if (vArr[j].hasPaid == status && vArr[j].type != NULL) {
      vArrSub[i] = vArr[j];
      i++;
    }
  }
}


// END DISPLAY FUNCTIONS



// Read incoming messages.
// Messages are capped at 30 characters including
// carriage returns and newlines.
// Returns true for a successful read.
// Returns false otherwise.
bool ReadMessage(char cmdArr[][13]) {
  // Buffer for serial input.
  char msg[31];
  
  // Write the received message to buffer, then dump the excess received data.
  Serial.readBytes(msg, 30);
  Serial.readString();

  int i = 0;

  // Iterate through the message for an expected NL or CR
  // indicating the end of the command.
  while (msg[i] != '\n' && msg[i] != '\r') {
    i++;
    if (i >= 31) {
      Serial.print(F("ERROR: Did not receive expected terminator within 30 characters \n"));
      return false;
    }
  }

  // Ensure the command doesn't begin or end with a dash, since
  // this would break the message parsing logic.
  if (msg[0] == '-' || msg[i-1] == '-') {
    Serial.print(F("ERROR: Invalid message format \n"));
    return false;
  }

  // Mark the end of the command with a null terminator.
  msg[i] = NULL;

  // Clear command array.
  // This prevents data from the previous command from being used.
  for (i = 0; i < 4; i++) {
    strlcpy(cmdArr[i], "\0\0\0\0\0\0\0\0\0\0\0\0\0" , 12);
  }

  // Split the message using dashes as a delimiter.
  // Write the results into the command array.
  char * token = strtok(msg, "-");
  i = 0;
      
  while (token != NULL) {
    strlcpy(cmdArr[i], token, 13);
    token = strtok(NULL, "-");
    i++;
  }

  return true;
}



// MESSAGE VALIDATION FUNCTIONS

// Validate the command type.
bool ValidCommand(char cmd[]) {

  // Ensure length of 1.
  if (cmd[1] != NULL || cmd[0] == NULL) {
    return false;
  }

  // Ensure command is in commands list.
  for (int i = 0; i < 6; i++) {
    if (cmd[0] == commands[i]) {
      return true;
    }
  }

  return false;
}


// Validate the reg number.
bool ValidRegNum(char reg[]) {

  // Ensure length of at most 7.
  if (reg[7] != NULL) {
    return false;
  }

  for (int i = 0; i < 7; i++) {

    // All characters must be alphanumeric.
    if (!isAlphaNumeric(reg[i])) {
      return false;
    }
    
    // All characters but the 3rd and 4th must be uppercase letters.
    else if ((i <= 1 || i >= 4) && (!isAlpha(reg[i]) || !isUpperCase(reg[i]))) {
      return false;
    }

    // The 3rd and 4th characters must be numbers (i.e. not letters).
    else if (i >= 2 && i <= 3 && isAlpha(reg[i])) {
      return false;
    }
  }

  return true;
}

// Validate given vehicle type.
bool ValidType(char type[]) {

  // Ensure length of 1.
  if (type[1] != NULL || type[0] == NULL) {
    return false;
  }

  // Ensure type is in types list.
  for (int i = 0; i < 6; i++) {
    if (type[0] == types[i]) {
      return true;
    }
  }

  return false;
}

// Validate given location.
bool ValidLocation(char loc[]) {
  
  int i = 0;

  // The location can only contain characters 0-9, A-Z (upper or lower) and '.'
  while (loc[i] != NULL) {
    if (!(isAlphaNumeric(loc[i]) || loc[i] == '.')) { 
      return false;
    }
    i++;
  }

  // Ensure length is 1-11 inclusive.
  if (i >= 12 || i == 0) {
    return false;
  }

  return true;
}

// END OF VALIDATION FUNCTIONS



// FUNCTIONS USED BY STATE LOGIC

// Find the vehicle with the give reg number in the vehicle array.
// Returns -1 if the array is full and the entry was not found.
// Otherwise returns and encoded number containing the index of
// the first empty entry in the array and:
// - The index the reg number was found at if found.
// - The index 99 if the reg number was not found.
// This works on the assumption that vArrLen is less than 99,
// would break otherwise but this is impossible due to memory constraints.
int FindVehicle(Vehicle vArr[], char vRegNum[]) {
  int firstEmpty = 0;
  int foundAt = 0;
  bool found = false;

  // Iterate through all populated array entries
  // and check if the reg number matches.
  while (firstEmpty <= vArrLen-1 && vArr[firstEmpty].type != NULL) {

    if (strcmp(vArr[firstEmpty].regNum, vRegNum) == 0) {
      foundAt = firstEmpty * 100;
      found = true;
      Serial.print(F("DEBUG: Found "));
      Serial.print(vRegNum);
      Serial.print(F(" at index "));
      Serial.print(firstEmpty);
      Serial.print(F(" \n"));
    }
    firstEmpty++;
  }

  // Return -1 if the array is full and the reg number wasn't found.
  // Set the "found index" to 99 if the array is not full but the reg number wasn't found.
  if (!found) {
    if (firstEmpty >= vArrLen) {
      return -1;
    }
    foundAt = 9900;
  }

  return foundAt + firstEmpty;
}

// Remove the vehicle with the given reg number.
// Shifts all following entries to the previous array position
// so that the array is still sorted in order of arrival time.
void RemoveVehicle(Vehicle vArr[], char vRegNum[]) {

  // Check if vehicle to remove is present and has paid.
  int firstEmpty = FindVehicle(vArr, vRegNum);
  int foundAt = firstEmpty / 100;
  firstEmpty = firstEmpty % 100;

  if (foundAt == 99 || foundAt == -1) {
    Serial.print(F("ERROR: Vehicle not found \n"));
    return;
  }
  else if (!vArr[foundAt].hasPaid) {
    Serial.print(F("ERROR: Cannot remove unpaid vehicle \n"));
    return;
  }

  if (firstEmpty >= vArrLen) {
    firstEmpty = vArrLen - 1;
  }

  // Shift all following entries left.
  for (int i = foundAt; i < firstEmpty; i++) {
    vArr[i] = vArr[i+1];
  }

  Vehicle temp;
  vArr[firstEmpty] = temp;

  Serial.print(F("DEBUG: Removed vehicle "));
  Serial.print(vRegNum);
  Serial.print(F(" \n"));
}

// Add or replace the vehicle with the given reg number, type and location.
void AddVehicle(Vehicle vArr[], char vRegNum[], char vType[], char vLoc[]) {

  if (!ValidType(vType)) {
    Serial.print(F("ERROR: Invalid vehicle type \n"));
    return;
  }

  else if (!ValidLocation(vLoc)) { 
    Serial.print(F("ERROR: Invalid location \n"));
    return;
  }

  int firstEmpty = FindVehicle(vArr, vRegNum);

  if (firstEmpty == -1) {
    Serial.print(F("ERROR: Vehicle limit reached \n"));
    return;
  }
  
  int foundAt = firstEmpty / 100;
  firstEmpty = firstEmpty % 100;

  int i = 0;

  // If not replacing a vehicle, add it to the
  // end of the array.
  if (foundAt == 99) {
    i = firstEmpty;
  }

  else {
    i = foundAt;

    if (!vArr[i].hasPaid) {
      Serial.print(F("ERROR: Cannot modify unpaid vehicle \n"));
      return;
    }

    // Do not replace the vehicle if it already has the same type and location.
    if (vArr[i].type == vType[0] && strcmp(vArr[i].location, vLoc) == 0) {
      Serial.print(F("ERROR: A vehicle with the same reg number, type and location already exists \n"));
      return;
    }

    // If existing vehicle has paid, remove it from the array
    // and set index pointing to the index to add the updated
    // vehicle information.
    RemoveVehicle(vArr, vRegNum);
    if (firstEmpty != 0) {
      i = firstEmpty - 1;
    }
  }
  
  Vehicle temp(vRegNum, vType[0], vLoc);
  vArr[i] = temp;

  StoreArray(vArr);

  // Clear the screen in case it replaces a location
  // with a shorter one to prevent lingering text.
  // If select is being held then there is no reason
  // to clear the screen.
  if (!SelectHeld()) {
    lcd.clear();
  }

  Serial.print(F("DEBUG: Added vehicle "));
  Serial.print(vRegNum);
  Serial.print(F(" \n"));
  Serial.print(F("DONE!\n"));
}

// Change the pay status of the vehicle with the given reg number.
void ChangePayStatus(Vehicle vArr[], char vRegNum[], char status[]) {

  int foundAt = FindVehicle(vArr, vRegNum) / 100;

  if (foundAt == 99 || foundAt == -1) {
    Serial.print(F("ERROR: Vehicle not found \n"));
    return;
  }

  // Update status and timestamps if status changes.
  if (strcmp(status, "PD") == 0) {
    if (vArr[foundAt].hasPaid) {
      Serial.print(F("ERROR: Vehicle already has this status \n"));
      return;
    }

    vArr[foundAt].hasPaid = true;
    vArr[foundAt].CreateExitTime();
    StoreVehicle(vArr[foundAt], foundAt, true);
    if (!SelectHeld()) {
      lcd.clear();
    }
    Serial.print(F("DONE!\n"));
    return;
  }

  if (strcmp(status, "NPD") == 0) {
    if (!vArr[foundAt].hasPaid) {
      Serial.print(F("ERROR: Vehicle already has this status \n"));
      return;
    }

    Vehicle temp = vArr[foundAt];
    char tType[2] = {temp.type, NULL};
    RemoveVehicle(vArr, temp.regNum);
    AddVehicle(vArr, temp.regNum, tType, temp.location);
    return;
  }

  Serial.print(F("ERROR: Invalid pay status given \n"));
}

// Change the type of the vehicle with the given reg number.
void ChangeType(Vehicle vArr[], char vRegNum[], char vType[]) {
  
  if (!ValidType(vType)) {
    Serial.print(F("ERROR: Invalid vehicle type \n"));
    return;
  }

  int foundAt = FindVehicle(vArr, vRegNum) / 100;
  if (foundAt == 99 || foundAt == -1) {
    Serial.print(F("ERROR: Vehicle not found \n"));
  }

  if (!vArr[foundAt].hasPaid) {
    Serial.print(F("ERROR: Cannot change type of unpaid vehicle \n"));
    return;
  }

  if (vArr[foundAt].type == vType[0]) {
    Serial.print(F("ERROR: Vehicle is already of this type \n"));
    return;
  }

  vArr[foundAt].type = vType[0];
  StoreVehicle(vArr[foundAt], foundAt, true);
  Serial.print(F("DONE!\n"));
}

// Change the location of the vehicle with the given reg number.
void ChangeLocation(Vehicle vArr[], char vRegNum[], char vLoc[]) {

  if (!ValidLocation(vLoc)) {
    Serial.print(F("ERROR: Invalid location \n"));
    return;
  }

  int foundAt = FindVehicle(vArr, vRegNum) / 100;
  if (foundAt == 99 || foundAt == -1) {
    Serial.print(F("ERROR: Vehicle not found \n"));
    return;
  }

  if (strcmp(vArr[foundAt].location, vLoc) == 0) {
    Serial.print(F("ERROR: Vehicle is already in this location \n"));
    return;
  }
  
  if (!vArr[foundAt].hasPaid) {
    Serial.print(F("ERROR: Cannot change location of unpaid vehicle \n"));
    return;
  }

  if (!SelectHeld()) {
    lcd.clear();
  }
  
  Vehicle temp = vArr[foundAt];
  char tType[2] = {temp.type, NULL};
  AddVehicle(vArr, temp.regNum, tType, vLoc);
}

// END OF FUNCTIONS USED BY STATE LOGIC



// MAIN CODE

// The setup function contains the synchronisation phase.
void setup() {

  // Initialise and set backlight to purple.
  Serial.begin(9600);
  Serial.setTimeout(100);

  lcd.begin(16, 2);
  lcd.setBacklight(5);

  lcd.createChar(0, upArrowArr);
  lcd.createChar(1, downArrowArr);

  // Creates an interrupt that fetches the amount of free
  // SRAM every half second.
  Timer1.initialize(500000);
  Timer1.attachInterrupt(UpdateFreeMem);

  // Set the time to 12:00:00 - 01/01/2023.
  // This is used as a failsafe incase there is no
  // timestamp stored in EEPROM.
  setTime(12, 0, 0, 1, 1, 2023);

  unsigned long timer = 0;
  unsigned long dTimer = 0;

  // This while loop awaits the synchronisation message 'X'.
  while (true) {

    if(!SelectHeld()) {
      lcd.clear();
    }

    // Send 'Q' every second during synchronisation.
    dTimer = millis();
    if (dTimer >= timer + 1000) {
      Serial.print(F("Q"));
      timer = dTimer;
    }
    
    // Receive inputs.
    if (Serial.available()) {
      
      // Finish synchronisation when the correct message is received.
      if (Serial.readString() == F("X")) {
        break;
      }

      // Display an error for incorrect messages.
      else {
        Serial.print(F("ERROR: Incorrect synchronisation message received, expected 'X' \n"));
      }
    }
  }

  // Finish synchronisation.
  // Send extension list and set backlight to white
  // if select is not being held.
  Serial.print(F("UDCHARS,FREERAM,HCI,EEPROM,SCROLL\n"));
  if (!SelectHeld()) {
    lcd.setBacklight(7);
  }
}


// The loop function contains the main phase, by default in the displaying state.
void loop() {

  // Current state
  static states state = DISPLAYING;

  // Currently displayed array
  static currentDisplay display = MAIN;

  // Vehicle array vArr to hold all vehicles.
  static Vehicle vArr[vArrLen];

  // Duplicate array which will be used to hold the subset
  // of paid/unpaid vehicles to display for the HCI extension.
  static Vehicle vArrSub[vArrLen];

  // Index of current page in vArr.
  static int index = 0;  

  // 2D array which will hold the sections of a received command.
  char cmdArr[4][13];

  // Read stored data from EEPROM when beginning main phase.
  // This includes stored vehicles and the time
  // of the last change to stored information.
  static bool hasRead = false;
  if (!hasRead) {
    if(ReadData(vArr)) {
      // Print the reg numbers of all vehicles retrieved from storage.
      Serial.print(F("DEBUG: Data retrieved - "));
      for (int i = 0; i < vArrLen; i++) {
        if (vArr[i].fromStorage) {
          Serial.print(vArr[i].regNum);
          Serial.print(F(" "));
        }
      }
      Serial.print(F("\n"));
    }

    else {
      Serial.print(F("DEBUG: Did not retrieve data \n"));
    }

    hasRead = true;
  }



  // State logic and transitions.
  switch(state) {
    case DISPLAYING: {

      // Change the array to display depending on
      // left and right presses.
      switch(display) {
        case MAIN: {
          index = UpdateDisplay(vArr, index, display);
        }
          break;

        case UNPAID: {
          FilterVehicleArray(vArrSub, vArr, false);
          index = UpdateDisplay(vArrSub, index, display);
        }
          break;

        case PAID: {
          FilterVehicleArray(vArrSub, vArr, true);
          index = UpdateDisplay(vArrSub, index, display);
        }
          break;
      }

      // Flag the array to be filtered depending on button presses.
      if (index == -1) {
        index = 0;
        display = UNPAID;
      }
      else if (index == -2) {
        index = 0;
        display = PAID;
      }
      else if (index == -3) {
        index = 0;
        display = MAIN;
      }

      if (!Serial.available()) {
        break;
      }

      // Read incoming messages from the serial port.
      // Skips the rest of the logic if a bad message was received.
      if (!ReadMessage(cmdArr)) {
        
        exitstate;
      }

      // Validate the command type given.
      if (!ValidCommand(cmdArr[0])) {
        Serial.print(F("ERROR: Invalid command format \n"));
        
        exitstate;
      }

      // Validates the reg number of the message,
      // as this is used in all commands.
      if (!ValidRegNum(cmdArr[1])) {
        Serial.print(F("ERROR: Invalid reg number \n"));
        
        exitstate;
      }


      // Transitions to the appropriate command handling state.
      switch(cmdArr[0][0]) {
        case 'A': {
          state = ADDING_VEHICLE;
        }
          break;

        case 'R': {
          state = REMOVING_VEHICLE;
        }
          break;

        case 'S': {
          state = CHANGING_PAY_STATUS;
        }
          break;

        case 'T': {
          state = CHANGING_TYPE;
        }
          break;

        case 'L': {
          state = CHANGING_LOCATION;
        }
          break;
      }
      break;
    }



    case ADDING_VEHICLE: {      
      AddVehicle(vArr, cmdArr[1], cmdArr[2], cmdArr[3]);
      
      exitstate;
    }



    case REMOVING_VEHICLE: {
      if (cmdArr[2][0] != NULL || cmdArr[3][0] != NULL) {
        Serial.print(F("ERROR: Excess parameters specified for command \n"));
      }
      else {
        RemoveVehicle(vArr, cmdArr[1]);
        StoreArray(vArr);
        Serial.print(F("DONE!\n"));
      }
      // Prints DONE! outside of remove function as remove is used within other functions
      // and we don't want to send DONE! twice for the same command.
      
      exitstate;
    }
  


    case CHANGING_PAY_STATUS: {
      if (cmdArr[3][0] != NULL) {
        Serial.print(F("ERROR: Excess parameters specified for command \n"));
      }
      else {
        ChangePayStatus(vArr, cmdArr[1], cmdArr[2]);
      }
      
      exitstate;
    }
    


    case CHANGING_TYPE: {
      if (cmdArr[3][0] != NULL) {
        Serial.print(F("ERROR: Excess parameters specified for command \n"));
      }
      else {
        ChangeType(vArr, cmdArr[1], cmdArr[2]);
      }
      
      exitstate;
    }
    


    case CHANGING_LOCATION: {
      if (cmdArr[3][0] != NULL) {
        Serial.print(F("ERROR: Excess parameters specified for command \n"));
      }
      else {
        ChangeLocation(vArr, cmdArr[1], cmdArr[2]);
      }
      
      exitstate;
    }
  }
}