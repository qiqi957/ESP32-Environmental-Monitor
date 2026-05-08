#include "Arduino.h"
#include "Wire.h"
#include "DHT.h"
#include "Arduino.h"
#include "Adafruit_GFX.h"
#include "Adafruit_SSD1306.h"
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include "SD.h"
#include "SPI.h"


//WiFi网络配置
const char* ssid = "zzzz"; // WiFi名称（SSID）
const char* password = "xxxxx"; // WiFi密码

//创建Web服务器对象，监听80端口
WebServer server(80);

//引脚定义
#define DHTPIN 4          // DHT11传感器连接到GPIO4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE); // 创建DHT对象

#define LIGHT_SENSOR_PIN 34 // 光敏电阻AO引脚连接到GPIO34

#define SD_CS_PIN 5 // SD卡模块的CS引脚连接到GPIO5

#define SCREEN_WIDTH 128 // OLED显示屏宽度，单位为像素
#define SCREEN_HEIGHT 64 // OLED显示屏高度，单位为像素
#define OLED_RESET     -1 // OLED显示屏复位引脚（如果没有连接则设置为-1）
#define OLED_ADDR 0x3C // OLED显示屏I2C地址
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);// 创建OLED显示屏对象

//全局变量
float humidity=0.0; // 湿度
float temperature=0.0; // 温度
int lightValue=0; // 光敏电阻值
int lightPercent=0; // 光照强度百分比
bool Wificonnected = false; // WiFi连接状态
bool SDCardMounted = false; // SD卡挂载状态
File uploadFile; // 用于处理文件上传的全局变量

//读取DHT11传感器数据的函数，返回湿度和温度
void readDHT11(float &humidity, float &temperature){
  humidity = dht.readHumidity();      // 读取湿度
  temperature = dht.readTemperature();   // 读取温度（默认单位为摄氏度）
} 

// 读取光敏电阻数据的函数，返回平均值以减少噪声影响
int readLightSensor(){
  long sum=0;
  for(int i=0;i<10;i++){
    sum+=analogRead(LIGHT_SENSOR_PIN);
    delay(100); 
  }
  return sum/10;
}

// 将光敏电阻值转换为百分比
void updateLightPercent() { 
  lightValue = readLightSensor(); // 获取光敏电阻的平均值
  lightPercent = map(lightValue, 4095, 0, 0, 100); // 将光敏电阻值映射到0-100%
}

//保存数据到SD卡的函数
void saveDataToSD(){
  if(!SDCardMounted) return; // 如果SD卡未挂载则直接返回

  File dataFile = SD.open("/sensor_data/data.csv", FILE_APPEND);
  if (!dataFile) {
    Serial.println("❌ 无法打开数据文件！");
    Serial.println("路径为: /sensor_data/data.csv");
    return;
  }

  //写入数据：温湿度，光照，时间戳
  dataFile.print(millis()); // 时间戳
  dataFile.print(",");
  dataFile.print(temperature, 1); // 温度，保留1位小数
  dataFile.print(",");
  dataFile.print(humidity, 1); // 湿度，保留1位小数
  dataFile.print(",");
  dataFile.println(lightPercent); // 光照强度百分比

  dataFile.close(); // 关闭文件
  Serial.println("数据已保存到SD卡");
}

//创建CVS文件头（只第一次运行时调用）
void createCVSHeader() {
  if(!SDCardMounted) return; // 如果SD卡未挂载则直接返回

  if (!SD.exists("/sensor_data/data.csv")) {
    File dataFile = SD.open("/sensor_data/data.csv", FILE_WRITE);
    if (dataFile) {
      dataFile.println("时间(ms),温度(°C),湿度(%),光照(%)");
      dataFile.close();
      Serial.println("📄 创建CSV文件并写入表头");
    }
  } else {
    Serial.println("📄 CSV文件已存在，无需创建");
  }
}

//获取环境状态字符串
String getEnvStatus() {
  if (temperature > 30 || lightPercent > 80) {
    return "WRONG ENV!";
  } else {
    return "NORMAL ENV";
  }
}

//处理网页请求的函数
void handleRoot() {
  // 从SD卡读取网页文件
  File file = SD.open("/index.html", "r");
  if (!file) {
      server.send(404, "text/plain", "404: index.html not found on SD card");
      Serial.println("❌ 无法打开 /index.html，请检查SD卡中是否有此文件");
      return;
  }
  
  server.streamFile(file, "text/html");
  file.close();
  Serial.println("✅ 网页已从SD卡发送");
}

// ========== 处理JSON数据 ==========
void handleJSON() {
    String json = "{";
    json += "\"temperature\":" + String(temperature, 1) + ",";
    json += "\"humidity\":" + String(humidity, 1) + ",";
    json += "\"light\":" + String(lightPercent);
    json += "}";
    server.send(200, "application/json", json);
}

// ========== 处理404 ==========
void handleNotFound() {
    server.send(404, "text/plain", "404: Not Found");
}

//文件上传处理函数
void handleFileUpload() {
    HTTPUpload& upload = server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        // 开始上传：创建文件
        String filename = "/" + upload.filename;
        uploadFile = SD.open(filename, FILE_WRITE);
        
        if (uploadFile) {
            Serial.print("✅ 开始上传: ");
            Serial.println(filename);
        } else {
            Serial.println("❌ 创建文件失败");
        }
        
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // 写入数据：边收边写，不占内存
        if (uploadFile) {
            uploadFile.write(upload.buf, upload.currentSize);
        }
        
    } else if (upload.status == UPLOAD_FILE_END) {
        // 上传完成
        if (uploadFile) {
            uploadFile.close();
            Serial.print("✅ 上传完成: ");
            Serial.print(upload.filename);
            Serial.print(" (");
            Serial.print(upload.totalSize);
            Serial.println(" 字节)");
        }
        
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        // 上传中断
        if (uploadFile) {
            uploadFile.close();
        }
        Serial.println("❌ 上传中断");
    }
}

//列出SD卡根目录下的文件
void handleFileList() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>SD卡文件列表</title>";
    html += "<style>body{font-family:Arial;margin:20px;} li{margin:5px 0;}</style>";
    html += "</head><body>";
    html += "<h1>📁 SD卡文件列表</h1><ul>";

    File root = SD.open("/");
    File file = root.openNextFile();
    
    while (file) {
        html += "<li>📄 /";
        html += file.name();
        html += " (";
        html += file.size();
        html += " 字节)</li>";
        file = root.openNextFile();
    }
    
    html += "</ul>";
    html += "<br><a href='/'>← 返回首页</a>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
    root.close();
}

//删除SD卡文件
void handleDeleteFile() {
    if (server.hasArg("file")) {
        String filename = "/" + server.arg("file");
        if (SD.remove(filename)) {
            server.send(200, "text/plain", "✅ 删除成功: " + server.arg("file"));
        } else {
            server.send(500, "text/plain", "❌ 删除失败");
        }
    } else {
        server.send(400, "text/plain", "请指定文件名");
    }
}

//初始化Web服务器
void initWebServer(){
  server.on("/", handleRoot); // 处理根路径请求
  server.on("/data", handleJSON); // 处理数据请求
  server.on("/list", handleFileList); // 处理文件列表请求
  server.on("/favicon.ico", []() { server.send(204, "text/plain", ""); }); // 处理favicon请求，返回204 No Content
  server.onNotFound(handleNotFound); // 处理404

  // 文件上传接口
  server.on("/upload", HTTP_POST, []() {
      server.send(200, "text/plain", "文件上传完成");
  }, handleFileUpload);
  
   // 文件删除接口
  server.on("/delete", handleDeleteFile);

  server.begin();
  Serial.println("Web服务器已启动");
  Serial.println("访问地址: http://" + WiFi.localIP().toString());
}

// 终端显示
void printToSerial() {
  Serial.println("================================");
  Serial.println("        --ENV MONITOR--        ");
  Serial.println("================================");
  
  Serial.print("  Temperature: ");
  Serial.print(temperature, 1);
  Serial.println(" C");
  
  Serial.print("  Humidity:    ");
  Serial.print(humidity, 1);
  Serial.println(" %");
  
  Serial.print("  Light:       ");
  Serial.print(lightPercent);
  Serial.println(" %");
  
  Serial.println("--------------------------------");
  Serial.print("  Status: ");
  Serial.println(getEnvStatus());
  Serial.println("================================");
  Serial.println();
}

// 更新OLED显示屏内容的函数
void updateDisplay() {
  display.clearDisplay();
  
  // ========== 顶部边框线 ==========
  display.drawLine(0, 0, SCREEN_WIDTH, 0, SSD1306_WHITE);
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  
  // ========== 标题 ==========
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(SCREEN_WIDTH / 2 - 35, 3);
  display.print("ENV MONITOR");
  
  // ========== WiFi状态图标（右上角）==========
  display.setCursor(SCREEN_WIDTH - 25, 3);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi");
  } else {
    display.print("X");
  }
  
  // ========== 温度 ==========
  display.setCursor(5, 16);
  display.print("Temp:");
  display.setCursor(55, 16);
  display.print(temperature, 1);
  display.print(" C");
  
  // ========== 湿度 ==========
  display.setCursor(5, 28);
  display.print("Humi:");
  display.setCursor(55, 28);
  display.print(humidity, 1);
  display.print(" %");
  
  // ========== 光照强度 ==========
  display.setCursor(5, 40);
  display.print("Light:");
  display.setCursor(55, 40);
  display.print(lightPercent);
  display.print(" %");
  
  // ========== 底部分隔线 ==========
  display.drawLine(0, 50, SCREEN_WIDTH, 50, SSD1306_WHITE);
  
  // ========== 底部状态 ==========
  String status = getEnvStatus();
  display.setCursor(SCREEN_WIDTH / 2 - status.length() * 3, 56);
  display.print(status);
  
  // 根据WiFi状态显示不同内容
  if (WiFi.status() == WL_CONNECTED) {
    // WiFi已连接：显示环境状态
    display.setCursor(SCREEN_WIDTH / 2 - status.length() * 3, 56);
    display.print(status);
  } else {
    // WiFi未连接：显示警告
    display.setCursor(SCREEN_WIDTH / 2 - 30, 56);
    display.print("WiFi: Disconnected");
  }
  
  // ========== 底部边框线 ==========
  display.drawLine(0, SCREEN_HEIGHT - 1, SCREEN_WIDTH, SCREEN_HEIGHT - 1, SSD1306_WHITE);
  
  display.display();
}

// 连接WiFi的函数
void connectToWiFi(){
  Serial.print("正在连接WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED&& attempts < 20){
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Wificonnected = true;
    Serial.println("WiFi连接成功！");
    Serial.print("IP地址: ");
    Serial.println(WiFi.localIP());
    Serial.println("网页地址: http://" + WiFi.localIP().toString());
  } else {
    Wificonnected = false;
    Serial.println("WiFi连接失败！");
  }
  
}


// 初始化SD卡
void initSDCard() {
  Serial.print("正在初始化SD卡...");

  //初始化SPI总线
  SPI.begin(18, 19, 23, SD_CS_PIN); // SCK, MISO, MOSI, CS
  SPI.setFrequency(1000000); // 设置SPI频率为1MHz
  if (!SD.begin(SD_CS_PIN, SPI, 1000000)) {
    Serial.println("❌ SD卡初始化失败！");
    Serial.println("请检查：");
    Serial.println("  1. VCC是否接3.3V（不是5V）");
    Serial.println("  2. 接线是否正确（CS→GPIO5, MOSI→GPIO23, MISO→GPIO19, SCK→GPIO18）");
    Serial.println("  3. SD卡是否插入并格式化为FAT32");
    SDCardMounted = false;
    return;
  }

  SDCardMounted = true;
  Serial.println("✅ SD卡初始化成功！");

  //获取SD卡信息
  uint64_t cardSize = SD.cardSize() / (1024 * 1024); // 转换为MB
  Serial.printf("SD卡容量:%llu MB\n", cardSize);

  //创建数据文件
  if(!SD.exists("/sensor_data/data.csv")) {
    File dataFile = SD.open("/sensor_data/data.csv", FILE_WRITE);
    if (dataFile) {
      dataFile.println("Timestamp,Temperature,Humidity,LightPercent");
      dataFile.close();
      Serial.println("数据文件已创建：/sensor_data/data.csv");
    } else {
      Serial.println("❌ 无法创建数据文件！");
    }
  } else {
    Serial.println("数据文件已存在：/sensor_data/data.csv");
  }
}


void setup() {
  Serial.begin(115200);
  delay(1000); // 等待串口稳定

  Serial.println("==========================================");
  Serial.println("          ESP32 智能环境监测系统");
  Serial.println("==========================================");

  // 初始化DHT传感器
  Serial.println("正在初始化DHT11传感器...");
  dht.begin();
  Serial.println("DHT11传感器已初始化，正在读取数据...");

  //配置光敏电阻
  Serial.println("光敏电阻初始化完成");

  // 初始化OLED显示屏
  Serial.println("正在初始化OLED显示屏...");
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED显示屏初始化失败！");
    while(true); // 停止程序
  }
  Serial.println("OLED显示屏初始化成功！");

  // 初始化SD卡
  initSDCard();
  // 创建CSV文件头（如果文件不存在）
  createCVSHeader();

  //连接WiFi
  connectToWiFi();

  // 初始化Web服务器
  if(Wificonnected){
    initWebServer();
  }

    // 开机画面
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(15, 25);
    display.println("System Ready");
    display.setCursor(25, 40);
    if (Wificonnected) {
        display.println("Web Server ON");
    } else {
        display.println("WiFi Failed");
    }
    display.display();

  delay(2000); // 等待传感器稳定
}

void loop() {
  static unsigned long lastReadTime = 0;// 上次读取数据的时间
  if(millis() - lastReadTime > 3000) { // 每3秒读取一次数据
    lastReadTime = millis();

  //读取DHT11传感器数据
  readDHT11(humidity, temperature); 
  //读取光敏电阻数据
  readLightSensor();
  //更新光照强度百分比
  updateLightPercent();

  //保存数据到SD卡
  saveDataToSD();

  //OLED显示屏内容
  updateDisplay();

  //终端显示
  printToSerial(); 
  }

  //检查WiFi连接状态，如果断开则尝试重新连接
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient(); // 处理客户端请求
  }
  
  delay(10); // 每三秒读取一次
}