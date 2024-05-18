//-----------------video
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h" //disable brownout problems
#include "soc/rtc_cntl_reg.h"  //disable brownout problems
#include "esp_http_server.h"
#include <EEPROM.h>            // read and write from flash memory

// define the number of bytes you want to access
#define EEPROM_SIZE 1
// ---------------sd 
#include "FS.h"
#include "SD_MMC.h"            // SD Card ESP32
//----------------servo
#include <ESP32Servo.h>
Servo servo1;  // create servo object to control a servo
Servo servo2;  // create servo object to control a servo

int servo1_pin = 14;    //set servo 1 pin  R/L
int servo2_pin = 15;   //set servo 1 pin  U/D
int deg = 5;
int v = 0;
int p1 = 0;
int p2= 0;

bool SD_ready = false;
const int num_line_html = 63;   //define arrray size, must know the html line, you can also use much less rows by just putting everything in same line
String html_line[num_line_html];
//----------------------------
//Replace with your network credentials
const char* ssid = "SSID";
const char* password = "PASSWORD";
WiFiClient client = NULL;

const char * photoPrefix = "/photo_";
int photoNumber = 0;

WiFiServer server(80);
String header;
int forwards = 0;  
// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

#define PART_BOUNDARY "123456789000000000000987654321"

  #define PWDN_GPIO_NUM     32
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM      0
  #define SIOD_GPIO_NUM     26
  #define SIOC_GPIO_NUM     27
  
  #define Y9_GPIO_NUM       35
  #define Y8_GPIO_NUM       34
  #define Y7_GPIO_NUM       39
  #define Y6_GPIO_NUM       36
  #define Y5_GPIO_NUM       21
  #define Y4_GPIO_NUM       19
  #define Y3_GPIO_NUM       18
  #define Y2_GPIO_NUM        5
  #define VSYNC_GPIO_NUM    25
  #define HREF_GPIO_NUM     23
  #define PCLK_GPIO_NUM     22
  #define LED_PIN           33
  #define LED_ON           LOW // - Pin is inverted.
  #define LED_OFF         HIGH 

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

static esp_err_t stream_handler(httpd_req_t *req){
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];
  //httpd_resp_set_hdr(req,"Access-Control-Allow-Origin", "*");
  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK){
    return res;
  }

  while(true){
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if(fb->width > 400){
        if(fb->format != PIXFORMAT_JPEG){
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if(!jpeg_converted){
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    if(res == ESP_OK){
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if(fb){
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf){
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if(res != ESP_OK){
      break;
    }
    //Serial.printf("MJPG: %uB\n",(uint32_t)(_jpg_buf_len));
  }
  return res;
}

void flashLED(int flashtime) {
  // If we have it; flash it.
    digitalWrite(LED_PIN, LED_ON);  // On at full power.
    delay(flashtime);               // delay
    digitalWrite(LED_PIN, LED_OFF); // turn Off
}



void startCameraServer(){
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };
  
  //Serial.printf("Starting web server on port: '%d'\n", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &index_uri);
  }
}

void servo_setup(){
  servo1.write(60);   
  servo2.write(60);   
}

void get_page(){
    File file = SD_MMC.open("/html_page.txt");
    if (!file) {
      Serial.println("Failed to open file for reading");
      return;
    }
    int i = 0;
    Serial.println("Reading file line by line:");
    while (file.available()) {
      String line = file.readStringUntil('\n');
      html_line[i] =  line;
      Serial.println(line);
      i++;
    }
    file.close();
}
void print_page(){
  for (int i =0 ; i< sizeof(html_line)/sizeof(html_line)[0];i++){
    client.println(html_line[i]);
  }
}

void takeSavePhoto(){
  
  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }
  EEPROM.begin(EEPROM_SIZE);
  photoNumber = EEPROM.read(0) + 1;
  
  String photoFileName = photoPrefix + String(photoNumber) + ".jpg";
  fs::FS & fs = SD_MMC;
  Serial.printf("Picture file name: %s\n", photoFileName.c_str());

  File file = fs.open(photoFileName.c_str(), FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file in writing mode");
  } else {
    file.write(fb -> buf, fb -> len);
    Serial.printf("Saved file to path: %s\n", photoFileName.c_str());
    ++photoNumber;
    EEPROM.write(0, photoNumber);
    EEPROM.commit();
  }
  file.close();
  esp_camera_fb_return(fb);
}

void listFiles() {
  File root = SD_MMC.open("/");
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }

  Serial.println("Files found on SD card:");

  while (File file = root.openNextFile()) {
    Serial.print("- ");
    Serial.println(file.name());
    file.close();
  }
  root.close();
}


void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  // Wi-Fi connection
 
  camera_config_t config;

  //servo1.attach(servo1_pin);
  //servo2.attach(servo2_pin);
  //servo_setup();
  
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; 
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ON);
  
   
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s,FRAMESIZE_VGA);

  //Serial.println("Starting SD Card");
  if(!SD_MMC.begin()){
    Serial.println("SD Card Mount Failed");
    }else{
      uint8_t cardType = SD_MMC.cardType();
      if(cardType == CARD_NONE){
        Serial.println("No SD Card attached");
        return;
      }else{
        SD_ready = true;
        }
    }   
  get_page();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.print("Camera Stream Ready! Go to: http://");
  Serial.print(WiFi.localIP());  
  // Start streaming web server
  startCameraServer();  
  // Start server
  server.begin();

}

void loop() {

   client = server.available();
   if (client) {                             // If a new client connects,
    currentTime = millis();
    previousTime = currentTime;
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()  && currentTime - previousTime <= timeoutTime) {  // loop while the client's connected
      currentTime = millis();
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        //Serial.write(c);                    // print it out the serial monitor
        header += c;
        //Serial.print(header); //CHECK THIS TO CHANGE THE PAGES?
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            
            //Serial.println(header);
                      /*
             * pos = header.indexOf("GET /left");
             * pv = header.indexOf("DE", pos);
             * fv = header.substring(pos+11,pv).toInt();             
             * fv = map(fv,-60,60,0,180); 
             * servo.write(fv);
             */
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            if (header.indexOf("GET /search") != -1) {
            // Toggle LED or perform desired action
            Serial.println("Search");
              listFiles();
              header="";
            }
            if (header.indexOf("GET /pic") != -1) {
            // Toggle LED or perform desired action
            Serial.println("pic");
              takeSavePhoto();
              header="";
            }
            
            if (header.indexOf("GET /up") != -1) {
              p1 =   header.indexOf("GET /up?degree=")+15;
              p2 = header.indexOf("DE",p1);
              deg = header.substring(p1,p2).toInt();
              deg = map(deg,-60,+60, 0,180);
              if (deg==0){ deg = 5;}
              // Toggle LED or perform desired action
              //Serial.println("up ");
              //Serial.print(deg);
              v = servo1.read()+deg;
              servo1.write(v);  
              header="";
            }
            if (header.indexOf("GET /down") != -1) {
              p1 =   header.indexOf("GET /down?degree=")+17;
              p2 = header.indexOf("DE",p1);
              deg = header.substring(p1,p2).toInt();
              deg = map(deg,-60,+60, 0,180);
              if (deg==0){ deg = 5;}
              // Toggle LED or perform desired action
              //Serial.println("down ");
              //Serial.print(deg);
              v = servo1.read()+deg;
              servo1.write(v); 
              header=""; 
            }
            if (header.indexOf("GET /right") != -1) {
              p1 =   header.indexOf("GET /right?degree=")+18;
              p2 = header.indexOf("DE",p1);
              deg = header.substring(p1,p2).toInt();
              deg = map(deg,-60,+60, 0,180);
              if (deg==0){ deg = 5;}
              // Toggle LED or perform desired action
              //Serial.println("right ");
              //Serial.print(deg);
              v = servo2.read()+deg; 
              servo2.write(v);  
              header="";
            }
            if (header.indexOf("GET /left") != -1) {
              p1 =   header.indexOf("GET /left?degree=")+17;
              p2 = header.indexOf("DE",p1);
              deg = header.substring(p1,p2).toInt();
              deg = map(deg,-60,+60, 0,180);
              if (deg==0){ deg = 5;}
              // Toggle LED or perform desired action
              Serial.println("left ");
              Serial.print(deg);
              v = servo2.read()+deg;
              servo2.write(v);  
              header="";
            }

            print_page();
                        
            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }
  delay(100);
}
