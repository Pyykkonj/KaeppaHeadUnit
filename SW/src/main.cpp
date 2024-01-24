#include <Arduino.h>
#include <EEPROM.h>
#include <HardwareSerial.h>
#include "EasyNextionLibrary.h"
#include "BluetoothA2DPSink.h"
#include "BluetoothA2DPCommon.h"
#include "version.h"

//#define HMI_UPDATE_MODE 1

#define BLUETOOTH_NAME "Kaeppa Head Unit " VERSION

// BCK == BCK
// WS == LCK
// DATA == DIN

/* I2S Pins */
#define BCK_PIN 4
#define DATA_PIN 2
#define WS_PIN 15

#define EEPROM_SIZE 12
#define BRIGHTNESS_FILE_ADDR 0

// Predefined brightness levels 0 to 100 %
static int BRIGHTNESS_LEVELS[] = {1, 5, 10, 40, 60, 100};
static int BRIGHTNESS_LEVELS_AMOUNT = sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]) -1 ;
int brightness_ind = 2;

/* I2S pin config */
i2s_pin_config_t my_pin_config = {
        .bck_io_num = BCK_PIN, 
        .ws_io_num = WS_PIN,  
        .data_out_num = DATA_PIN, 
        .data_in_num = I2S_PIN_NO_CHANGE
};

// Serial ports for HMI display and for debug prints
HardwareSerial HmiSerialPort(2);  //if using UART2
HardwareSerial MonitorSerialPort(0);  

// Init HMI display
EasyNex myNex(HmiSerialPort);

// Init bluetooth A2DP
BluetoothA2DPSink a2dp_sink;

/* State enums */
enum Bluetooth_state_enum {
  DISCONNECTED,
  CONNECTING,
  CONNECTED  
};

const char * const Bluetooth_state_str[] =
{
    [DISCONNECTED] = "DISCONNECTED",
    [CONNECTING] = "CONNECTING",
    [CONNECTED]  = "CONNECTED"
};

struct Last_connection {
  uint8_t name_len;
  String name;
  esp_bd_addr_t mac;
};

Bluetooth_state_enum Bluetooth_state = CONNECTED;
esp_a2d_audio_state_t Audio_state = ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND;
bool source_name_got = false;

void sendTriggerReceivedOk(){
  myNex.writeNum("sig_ok_lab.bco", 2016);
  delay(50);
  myNex.writeNum("sig_ok_lab.bco", 32335);
}

void updateScreenBrightness(bool direction){
  if(direction){
    brightness_ind++;
    if(brightness_ind > BRIGHTNESS_LEVELS_AMOUNT){
      brightness_ind = BRIGHTNESS_LEVELS_AMOUNT;
    }
  } else {
    brightness_ind--;
    if(brightness_ind < 0){
      brightness_ind = 0;
    }
  }

  int brightness = BRIGHTNESS_LEVELS[brightness_ind];

  String br = "dim=" + String(brightness);
  myNex.writeStr(br);

  EEPROM.write(BRIGHTNESS_FILE_ADDR, brightness_ind);
  EEPROM.commit();

  MonitorSerialPort.println("Screen brightness: " + String(brightness));
}

// Previous button trigger
void trigger0(){
  MonitorSerialPort.print("Previous Button triggered, new state: ");
  a2dp_sink.previous();
  sendTriggerReceivedOk();
}

// Play/pause button trigger
void trigger1(){
  MonitorSerialPort.print("Play/Pause Button triggered\n");
  if(Audio_state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND || Audio_state == ESP_A2D_AUDIO_STATE_STOPPED){
    a2dp_sink.play();
  } else if (Audio_state == ESP_A2D_AUDIO_STATE_STARTED) {
    a2dp_sink.pause();
  }
  sendTriggerReceivedOk();
}

// Next button trigger
void trigger2(){
  MonitorSerialPort.print("Next Button triggered\n");
  a2dp_sink.next();
  sendTriggerReceivedOk();
}

// Bluetooth disconnect button trigger
void trigger3(){
  MonitorSerialPort.print("Bluetooth Button triggered\n");
  if(Bluetooth_state == CONNECTED){
    MonitorSerialPort.print("Disconnect bluetooth\n");
    a2dp_sink.disconnect();
  } else {
    // Seems to be fastest way to reconnect?
    ESP.restart();
  }
  
  sendTriggerReceivedOk();
}

// Brightness increase trigger
void trigger4(){
  MonitorSerialPort.print("Brightness increase triggered\n");
  updateScreenBrightness(true);
  sendTriggerReceivedOk();
}

// Brightness decrease trigger
void trigger5(){
  MonitorSerialPort.print("Brightness decrease triggered\n");
  updateScreenBrightness(false);
  sendTriggerReceivedOk();
}

void avrc_metadata_callback(uint8_t id, const uint8_t *text) {
  //MonitorSerialPort.printf("==> AVRC metadata rsp: attribute id 0x%x, %s\n", id, text);

  // Song received
  if(id == 0x1){
    String song = String((char*) text);
    MonitorSerialPort.println(song);
    myNex.writeStr("song_lab.txt", song);

  // Artist received
  } else if(id == 0x2){
    String artist = String((char*) text);
    MonitorSerialPort.println(artist);
    myNex.writeStr("artist_lab.txt", artist);
  }

}

void update_play_pause_button(esp_a2d_audio_state_t state){
  // Change play/pause button text
  if(state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND || state == ESP_A2D_AUDIO_STATE_STOPPED){
    myNex.writeStr("play_pause_but.txt", "Play");
  } else if (state == ESP_A2D_AUDIO_STATE_STARTED) {
    myNex.writeStr("play_pause_but.txt", "Pause");
  }
}

void audio_state_changed_callback(esp_a2d_audio_state_t state, void *ptr) {
  MonitorSerialPort.println(a2dp_sink.to_str(state));

  Audio_state = state;
  update_play_pause_button(Audio_state);
}

void printBlMac(esp_bd_addr_t addr){
  MonitorSerialPort.println(a2dp_sink.to_str(addr));
}

void printCurrentBlMac(){
  MonitorSerialPort.print("Current connection: ");
  esp_bd_addr_t* lastAddr = a2dp_sink.get_current_peer_address();
  printBlMac(*lastAddr);
}

void printLastBlMac(){
  MonitorSerialPort.print("Last connection: ");
  esp_bd_addr_t* lastAddr = a2dp_sink.get_last_peer_address();
  printBlMac(*lastAddr);
}


void updateBluetoothLabel(){

  if(Bluetooth_state == CONNECTED){

    const char* bl_source = a2dp_sink.get_connected_source_name();

    if (String(bl_source).isEmpty()){
      source_name_got = false;
      myNex.writeStr("bl_state_lab.txt", "CONNECTED");
      myNex.writeNum("bl_state_lab.bco", 1032);
      myNex.writeStr("bl_mac_lab.txt", " ");
      delay(50);
      myNex.writeNum("bl_mac_lab.bco", 1032);
    } else {
      source_name_got = true;
      myNex.writeStr("bl_state_lab.txt", "CONNECTED");
      myNex.writeNum("bl_state_lab.bco", 1032);
      myNex.writeStr("bl_mac_lab.txt", String(bl_source) );
      delay(50);
      myNex.writeNum("bl_mac_lab.bco", 1032);
    }

    MonitorSerialPort.println("Bluetooth state: CONNECTED, " + String(bl_source));
    digitalWrite(LED_BUILTIN, HIGH);
  } else if (Bluetooth_state == DISCONNECTED){
    esp_bd_addr_t* lastAddr = a2dp_sink.get_last_peer_address();

    myNex.writeStr("bl_state_lab.txt", "DISCONNECTED");
    myNex.writeNum("bl_state_lab.bco", 64528);
    myNex.writeStr("bl_mac_lab.txt", String(a2dp_sink.to_str(*lastAddr) ));
    myNex.writeNum("bl_mac_lab.bco", 64528);
    MonitorSerialPort.println("Bluetooth state: DISCONNECTED");
    myNex.writeStr("artist_lab.txt", "");
    myNex.writeStr("song_lab.txt", "");
    digitalWrite(LED_BUILTIN, LOW);
  }
  
}

void printCurrentBlState(){
  MonitorSerialPort.print("Current Bluetooth state: " + String(Bluetooth_state_str[Bluetooth_state]) + "\n");
}

void checkBluetoothState(){

  Bluetooth_state_enum old_state = Bluetooth_state;

  Bluetooth_state_enum new_state;
  
  if(a2dp_sink.is_connected() == true){
    new_state = CONNECTED;
  } else {
    new_state = DISCONNECTED;
  }

  if (new_state != old_state){
    if(new_state == CONNECTED){
      a2dp_sink.play();
      Audio_state = a2dp_sink.get_audio_state();
      update_play_pause_button(Audio_state);
    }
    Bluetooth_state = new_state;
    updateBluetoothLabel();
    printLastBlMac();
    printCurrentBlMac();
    printCurrentBlState();
  }

  if (Bluetooth_state == CONNECTED && !source_name_got){
    updateBluetoothLabel();
  }
}

// Useless :D
void runBootScreen(){
  myNex.writeStr("boot_lab.txt", "Initializing...");
  delay(2000);

  myNex.writeStr("boot_lab.txt", "Init done");
  delay(1000);

  myNex.writeStr("boot_lab.txt", "Welcome!");
  delay(2000);

  MonitorSerialPort.print("Welcome\n");

  myNex.writeStr("page page1");
}

void setup() {

  #ifdef HMI_UPDATE_MODE
    MonitorSerialPort.begin(9600, SERIAL_8N1, 3, 1);
    HmiSerialPort.begin(9600, SERIAL_8N1);
    MonitorSerialPort.println("Kaeppa Head Unit software startup in HMI UPDATE MODE, version " + String(VERSION));
    return;
  #endif

  MonitorSerialPort.begin(9600, SERIAL_8N1, 3, 1);

  EEPROM.begin(EEPROM_SIZE);

  myNex.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT); // The built-in LED is initialized as an output 
  
  MonitorSerialPort.println("Kaeppa Head Unit software startup, version " + String(VERSION));
  
  a2dp_sink.set_pin_config(my_pin_config);
  a2dp_sink.set_auto_reconnect(true, 10000);

  a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
  int metadataMask = 0 | ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST;

  a2dp_sink.set_on_audio_state_changed(audio_state_changed_callback);
  a2dp_sink.set_avrc_metadata_attribute_mask(metadataMask);
  a2dp_sink.start(BLUETOOTH_NAME);

  printLastBlMac();
  printCurrentBlMac();

  // Read & write screen brightness
  int brightness_ind_read = EEPROM.read(BRIGHTNESS_FILE_ADDR);
  MonitorSerialPort.println("Read last brightness_ind " + String(brightness_ind_read));  
  if((brightness_ind_read >= 0) && (brightness_ind_read <= BRIGHTNESS_LEVELS_AMOUNT))
  { 
    brightness_ind = brightness_ind_read;
    int brightness = BRIGHTNESS_LEVELS[brightness_ind];
    String br = "dim=" + String(brightness);
    myNex.writeStr(br);
  } 
}

void loop() {

  #ifdef HMI_UPDATE_MODE
    while(true)
    {
      if( MonitorSerialPort.available())
      {
        HmiSerialPort.write(MonitorSerialPort.read());
      }
      if( HmiSerialPort.available())
      {
        MonitorSerialPort.write(HmiSerialPort.read());
      }
    }
  #endif

  myNex.NextionListen();
  checkBluetoothState();
}

