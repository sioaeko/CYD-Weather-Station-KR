/*
 * CYD 화면 진단 2: 정식 펌웨어와 같은 TFT_eSPI 설정(libraries/TFT_eSPI-2.5.44.zip)으로
 *   A = 백라이트 digitalWrite HIGH (PWM 없음)
 *   B = 백라이트 PWM 최대 (ledcAttach + ledcWrite 255)
 *   C = 백라이트 PWM 어둡게 (ledcWrite 40)
 * 를 8초씩 번갈아 보여줍니다. 어느 글자가 보이는지로 원인을 가립니다.
 */
#include <TFT_eSPI.h>
TFT_eSPI tft;
constexpr int BL = 21;

void show(const char* label, uint16_t bg) {
  tft.fillScreen(bg);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(4);
  tft.drawString(label, tft.width() / 2, tft.height() / 2, 4);
  tft.setTextSize(1);
  tft.drawString("CYD TEST", tft.width() / 2, 24, 4);
}
void setup() {
  Serial.begin(115200);
  tft.begin(); tft.setRotation(1);
  Serial.printf("\nTFT_eSPI setup=%d size=%dx%d\n", USER_SETUP_ID, tft.width(), tft.height());
}
void loop() {
  ledcDetach(BL); pinMode(BL, OUTPUT); digitalWrite(BL, HIGH);
  show("A", TFT_BLUE); Serial.println("TEST A digitalWrite HIGH"); delay(8000);
  const bool ok = ledcAttach(BL, 5000, 8);
  ledcWrite(BL, 255); show("B", TFT_DARKGREEN); Serial.printf("TEST B pwm 255 attach=%d\n", ok); delay(8000);
  ledcWrite(BL, 40); show("C", TFT_MAROON); Serial.println("TEST C pwm 40"); delay(8000);
}
