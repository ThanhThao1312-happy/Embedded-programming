#include "Arduino.h"
#include "WiFi.h"
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems
#include "driver/rtc_io.h"
#include <LittleFS.h>
#include <FS.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include <esp_task_wdt.h>
#include "esp_camera.h"
#include "model.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Khai báo WiFi
const char* ssid = "huywifi";
const char* password = "12345678";

// Khai báo Firebase
#define API_KEY "AIzaSyCw85h4ynNMqUsULKi06ic399BL4yC6ffg"
#define USER_EMAIL "voquanghuy08102000@gmail.com"
#define USER_PASSWORD "huy01667717825"
#define STORAGE_BUCKET_ID "firedetection-ab06f.appspot.com"
#define FILE_PHOTO_PATH "/photo.jpg"
#define BUCKET_PHOTO "/data/photo.jpg"
//realtime database
#define DATABASE_URL "https://firedetection-ab06f-default-rtdb.firebaseio.com/" 

// Kích thước ảnh
#define RESIZED_WIDTH 32
#define RESIZED_HEIGHT 32

namespace {
tflite::ErrorReporter* error_reporter = nullptr;
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;
int inference_count = 0;
SemaphoreHandle_t mutex = NULL;
constexpr int kTensorArenaSize = 10000;
uint8_t tensor_arena[kTensorArenaSize];
}  // namespace


// Khai báo các chân camera
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

// Biến cờ xác định việc chụp ảnh mới
boolean takeNewPhoto = true;

// Đối tượng Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig configF;

unsigned long sendDataPrevMillis = 0;
int count = 0;
bool signupOK = false;

// Biến đếm số lần dự đoán là đám cháy
int firePredictionCount = 0;
void fcsUploadCallback(FCS_UploadStatusInfo info);
// Hàm chụp ảnh và lưu vào LittleFS
void capturePhotoSaveLittleFS() {
    // Dispose first pictures because of bad quality
    camera_fb_t* fb = NULL;
    // Skip first 3 frames (increase/decrease number as needed).
    for (int i = 0; i < 4; i++) {
        fb = esp_camera_fb_get();
        esp_camera_fb_return(fb);
        fb = NULL;
    }

    // Take a new photo
    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        delay(1000);
        ESP.restart();
    }

    // Photo file name
    Serial.printf("Picture file name: %s\n", FILE_PHOTO_PATH);
    File file = LittleFS.open(FILE_PHOTO_PATH, FILE_WRITE);

    // Insert the data in the photo file
    if (!file) {
        Serial.println("Failed to open file in writing mode");
    }
    else {
        // Write the payload (image) to the file
        file.write(fb->buf, fb->len);
        Serial.print("The picture has been saved in ");
        Serial.print(FILE_PHOTO_PATH);
        Serial.print(" - Size: ");
        Serial.print(fb->len);
        Serial.println(" bytes");
    }

    // Close the file
    file.close();

    // Convert the image format from PIXFORMAT_GRAYSCALE to PIXFORMAT_JPEG
    size_t jpg_buf_len;
    uint8_t *jpg_buf;
    bool jpeg_converted = frame2jpg(fb, 80, &jpg_buf, &jpg_buf_len);
    if (!jpeg_converted) {
        Serial.println("JPEG compression failed");
        esp_camera_fb_return(fb);
        return;
    }

    // Save the converted JPEG image to a new file
    File jpeg_file = LittleFS.open("/photo.jpg", FILE_WRITE);
    if (!jpeg_file) {
        Serial.println("Failed to open JPEG file in writing mode");
    } else {
        jpeg_file.write(jpg_buf, jpg_buf_len);
        Serial.println("Converted JPEG image saved");
    }

    // Close the JPEG file and release the memory
    jpeg_file.close();
    esp_camera_fb_return(fb);
    free(jpg_buf);
}

// Khởi tạo WiFi
void initWiFi(){
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
}

// Khởi tạo LittleFS
void initLittleFS(){
    if (!LittleFS.begin(true)) {
        Serial.println("An Error has occurred while mounting LittleFS");
        ESP.restart();
    }
    else {
        delay(500);
        Serial.println("LittleFS mounted successfully");
    }
}

// Khởi tạo camera
bool initCamera() {
    camera_config_t config;
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
    config.xclk_freq_hz = 16000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;

    esp_err_t result = esp_camera_init(&config);

    if (result != ESP_OK) {
        return false;
    }
    return true;
}

// Hàm lấy ảnh từ camera và đưa vào input tensor
TfLiteStatus GetImage(tflite::ErrorReporter* error_reporter, int image_width,
                      int image_height, int channels, uint8_t* image_data) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return kTfLiteError;
    }

    float scale_x = static_cast<float>(fb->width) / image_width;
    float scale_y = static_cast<float>(fb->height) / image_height;

    for (int y = 0; y < image_height; ++y) {
        for (int x = 0; x < image_width; ++x) {
            int src_x = static_cast<int>(x * scale_x);
            int src_y = static_cast<int>(y * scale_y);
            uint8_t pixel_value = fb->buf[src_y * fb->width + src_x];
            image_data[y * image_width + x] = pixel_value;
        }
    }
    esp_camera_fb_return(fb);
    return kTfLiteOk;
}
void sendDataTask(void * parameter) {
  // Khai báo biến lưu thời điểm gửi dữ liệu cuối cùng
  TickType_t lastSendTime = xTaskGetTickCount();

  while (true) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
    // Kiểm tra thời gian kể từ lần gửi dữ liệu cuối cùng
    TickType_t currentTime = xTaskGetTickCount();
    TickType_t elapsedTime = currentTime - lastSendTime;

    // Nếu đã đủ thời gian, gửi dữ liệu lên Firebase
    if (elapsedTime >= pdMS_TO_TICKS(1000)) {
        while (Serial.available() > 0) {
        char c = Serial.read(); // Đọc và loại bỏ dữ liệu từ bộ nhớ đệm
        }
      delay(250);
     if (Serial.available() > 0) {
        String data = Serial.readStringUntil('\n');
        // Process the received data (temperature, humidity, analog value)
        // Split the data using ',' as delimiter
        int commaIndex1 = data.indexOf(',');
        int commaIndex2 = data.indexOf(',', commaIndex1 + 1);

        if (commaIndex1 != -1 && commaIndex2 != -1) {
            String temperatureStr = data.substring(0, commaIndex1);
            String humidityStr = data.substring(commaIndex1 + 1, commaIndex2);
            String analogValueStr = data.substring(commaIndex2 + 1);
            if (Firebase.ready() && signupOK) {
            float temperature = temperatureStr.toFloat();
            float humidity = humidityStr.toFloat();
            int MQ2 = analogValueStr.toInt();

            // Send data to Firebase Realtime Database
            Firebase.RTDB.setFloat(&fbdo, "Temperature", temperature);
            Firebase.RTDB.setFloat(&fbdo, "Humidity", humidity);
            Firebase.RTDB.setInt(&fbdo, "MQ2", MQ2);
            }
            // Cập nhật thời gian gửi dữ liệu cuối cùng
               lastSendTime = currentTime;
        }}
         xSemaphoreGive(mutex);}
        } else {
            // Xử lý lỗi không thể lấy mutex
        }
    vTaskDelay(1000);
    }}

// //Test data ramdom
//     void sendDataTask(void * parameter) {
//   // Khai báo biến lưu thời điểm gửi dữ liệu cuối cùng
//   TickType_t lastSendTime = xTaskGetTickCount();

//   while (true) {
//      if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
//     // Kiểm tra thời gian kể từ lần gửi dữ liệu cuối cùng
//     TickType_t currentTime = xTaskGetTickCount();
//     TickType_t elapsedTime = currentTime - lastSendTime;

//     // Nếu đã đủ thời gian, gửi dữ liệu lên Firebase
//     if (elapsedTime >= pdMS_TO_TICKS(1000)) {
//       if (Firebase.ready() && signupOK) {
//         // Tạo dữ liệu ngẫu nhiên cho nhiệt độ từ 10 đến 40 độ C
//         float randomTemperature = random(10, 41);
//         // Tạo dữ liệu ngẫu nhiên cho độ ẩm từ 40% đến 80%
//         float randomHumidity = random(40, 81);
//         // Tạo dữ liệu ngẫu nhiên cho giá trị MQ2 từ 0 đến 1023
//         int randomMQ2 = random(0, 1024);

//         // Gửi dữ liệu ngẫu nhiên lên Firebase
//         Firebase.RTDB.setFloat(&fbdo, "RandomTemperature", randomTemperature);
//         Firebase.RTDB.setFloat(&fbdo, "RandomHumidity", randomHumidity);
//         Firebase.RTDB.setInt(&fbdo, "RandomMQ2", randomMQ2);
//       }

//       // Cập nhật thời gian gửi dữ liệu cuối cùng
//       lastSendTime = currentTime;
//     }
//         xSemaphoreGive(mutex);
//         } else {
//             // Xử lý lỗi không thể lấy mutex
//         }
//     // Chờ 1 giây trước khi kiểm tra điều kiện gửi dữ liệu tiếp theo
//     vTaskDelay(1000);
//   }
// }

  void sendImageTask(void * parameter) {
    while (true) {
      if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
         if (kTfLiteOk != GetImage(error_reporter, RESIZED_WIDTH, RESIZED_HEIGHT, 1, input->data.uint8)) {
        TF_LITE_REPORT_ERROR(error_reporter, "Image capture failed.");
    }

    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
        TF_LITE_REPORT_ERROR(error_reporter, "Invoke failed.");
    }

    uint8_t predicted = output->data.uint8[0];
    float percentage = (predicted / 255.0) * 100;

    Serial.print("Dự đoán: ");
    Serial.print(percentage);
    Serial.println("% ");
    if (Firebase.ready() && signupOK) {
        // Gửi giá trị dự đoán (predicted) lên Firebase Realtime Database
        if (Firebase.RTDB.setFloat(&fbdo, "Prediction", percentage)) {
            Serial.println("Prediction sent successfully");
            Serial.println("Predicted percentage: " + String(percentage));
        } else {
            Serial.println("Failed to send prediction");
            Serial.println("Reason: " + fbdo.errorReason());
        }
    if (percentage > 70) {
        Serial.println("Fire");
        // Nếu dự đoán là đám cháy, tăng biến đếm lên 1
        firePredictionCount++;
        // Nếu đã có 3 dự đoán liên tiếp là đám cháy, thực hiện chụp ảnh và gửi lên Firebase
        if (firePredictionCount >= 3) {
            Serial.println("Chụp ảnh và gửi lên Firebase");
            capturePhotoSaveLittleFS();
                Serial.print("Uploading picture... ");
                if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, FILE_PHOTO_PATH, mem_storage_type_flash, BUCKET_PHOTO, "image/jpeg", fcsUploadCallback)) {
                    Serial.printf("\nDownload URL: %s\n", fbdo.downloadURL().c_str());
                     if (Firebase.ready() && signupOK) {
                     if (Firebase.RTDB.setString(&fbdo, "ImageURL", fbdo.downloadURL().c_str())) {
            Serial.println("ImageURL sent successfully");
        } else {
            Serial.println("Failed to send ImageURL");
             Serial.println("Reason: " + fbdo.errorReason());
        }}
                } else {
                    Serial.println(fbdo.errorReason());
                }
            // Reset biến đếm về 0 sau khi thực hiện chụp và gửi ảnh
            firePredictionCount = 0;
        }
    } else {
        Serial.println("Non-fire");
        // Nếu không phải đám cháy, reset biến đếm về 0
        firePredictionCount = 0;
    }}
       xSemaphoreGive(mutex);
        } else {
            // Xử lý lỗi không thể lấy mutex
        }
    vTaskDelay(2000);
    }
  }

void setup() {
    Serial.begin(115200);
    initWiFi();
    initLittleFS();

    // Turn-off the 'brownout detector'
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    if (!initCamera()) {
        Serial.println("Failed to initialize camera...");
        return;
    }

    //Firebase
    // Assign the api key
    configF.api_key = API_KEY;
    //Assign the user sign in credentials
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    configF.database_url = DATABASE_URL;
      if (Firebase.signUp(&configF, &auth, "", "")){
    Serial.println("ok");
    signupOK = true;
  }
  else{
    Serial.printf("%s\n", configF.signer.signupError.message.c_str());
  }
    //Assign the callback function for the long running token generation task
    configF.token_status_callback = tokenStatusCallback;
      /* Sign up */

    Firebase.begin(&configF, &auth);
    Firebase.reconnectWiFi(true);
    
    static tflite::MicroErrorReporter micro_error_reporter;
    error_reporter = &micro_error_reporter;

    // Load model
    model = tflite::GetModel(g_model);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        TF_LITE_REPORT_ERROR(error_reporter,
                             "Model provided is schema version %d not equal "
                             "to supported version %d.",
                             model->version(), TFLITE_SCHEMA_VERSION);
        return;
    }

    static tflite::AllOpsResolver resolver;
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, kTensorArenaSize, error_reporter);
    interpreter = &static_interpreter;

    TfLiteStatus allocate_status = interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        TF_LITE_REPORT_ERROR(error_reporter, "AllocateTensors() failed");
        return;
    }

    input = interpreter->input(0);
    output = interpreter->output(0);
    mutex = xSemaphoreCreateMutex();
    esp_task_wdt_init(10000, false);
    if (mutex == NULL) {
        Serial.println("Failed to create mutex");
        // Xử lý lỗi tại đây
    }
    inference_count = 0;
    xTaskCreatePinnedToCore(
    sendDataTask,          // Callback function của task
    "sendDataTask",        // Tên của task
    10000,                  // Kích thước stack của task
    NULL,                  // Tham số truyền vào task
    1,                     // Task priority
    NULL,                  // Task handle
    1                     // Core số 0
  );
    xTaskCreatePinnedToCore(
    sendImageTask,          // Callback function của task
    "sendImageTask",        // Tên của task
   10000,              // Kích thước stack của task
    NULL,                  // Tham số truyền vào task
    2,                     // Task priority
    NULL,                  // Task handle
    1                    // Core số 0
  );
}

void loop() 
{
  esp_task_wdt_reset();
}

// Callback hàm cho việc tải lên Firebase
void fcsUploadCallback(FCS_UploadStatusInfo info) {
    if (info.status == firebase_fcs_upload_status_init) {
        Serial.printf("Uploading file %s (%d) to %s\n", info.localFileName.c_str(), info.fileSize, info.remoteFileName.c_str());
    } else if (info.status == firebase_fcs_upload_status_upload) {
        Serial.printf("Uploaded %d%s, Elapsed time %d ms\n", (int)info.progress, "%", info.elapsedTime);
    } else if (info.status == firebase_fcs_upload_status_complete) {
        Serial.println("Upload completed\n");
        FileMetaInfo meta = fbdo.metaData();
        Serial.printf("Name: %s\n", meta.name.c_str());
        Serial.printf("Bucket: %s\n", meta.bucket.c_str());
        Serial.printf("contentType: %s\n", meta.contentType.c_str());
        Serial.printf("Size: %d\n", meta.size);
        Serial.printf("Generation: %lu\n", meta.generation);
        Serial.printf("Metageneration: %lu\n", meta.metageneration);
        Serial.printf("ETag: %s\n", meta.etag.c_str());
        Serial.printf("CRC32: %s\n", meta.crc32.c_str());
        Serial.printf("Tokens: %s\n", meta.downloadTokens.c_str());
        Serial.printf("Download URL: %s\n\n", fbdo.downloadURL().c_str());
    } else if (info.status == firebase_fcs_upload_status_error) {
        Serial.printf("Upload failed, %s\n", info.errorMsg.c_str());
    }
}
